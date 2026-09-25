// SPDX-License-Identifier: Apache-2.0
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NANOHUB_DEVICE "/dev/nanohub"

/*
 * Event and sensor identifiers from the hub's SEOS firmware. A sensor reports
 * its samples as event EVT_SENSOR_BASE + sensor type; the host enables and
 * disables a sensor by writing a config event.
 */
#define EVT_SENSOR_BASE 0x00000200u
#define EVT_CONFIG 0x00000300u

#define SENSOR_RATE_ONCHANGE 0xffffff01u

/* Sample rates travel as Q10 fixed point: 1 Hz is 1 << 10. */
#define RATE_Q10_SHIFT 10
#define MAX_RATE_HZ 800.0

/*
 * Sensor types, as the stock Mobvoi firmware numbers them. These match the
 * kernel driver's custom_app_event.h, which puts the PPG heart rate at 16.
 *
 * This covers the hub's own sensors: accelerometer, gyroscope, barometer
 * (with its temperature channel), ambient light and the PPG used for heart
 * rate and SpO2. NFC is not a hub sensor and is not reachable from here.
 */
#define SENS_TYPE_ACCEL 1
/*
 * The hub streams accelerometer samples under its own raw type rather than
 * the type used to enable it, and packs them as 16-bit counts instead of
 * floats. At rest the vector reads about 4096 counts, i.e. 1 g.
 */
#define SENS_TYPE_ACCEL_RAW 32
#define ACCEL_COUNTS_PER_G 4096.0f
#define STANDARD_GRAVITY 9.80665f
#define SENS_TYPE_GYRO 6
#define SENS_TYPE_BARO 10
#define SENS_TYPE_TEMP 11
#define SENS_TYPE_ALS 12
#define SENS_TYPE_HEARTRATE_PPG 16
/*
 * The battery gauge is a hub sensor rather than a power-supply device: the
 * kernel binds no fuel-gauge driver on this board. It reports charge and
 * terminal voltage in the first two fields of a three-field sample; the third
 * field is a constant the gauge does not populate.
 */
#define SENS_TYPE_FUELGAUGE 68
#define SENS_TYPE_STATIC_PPG 105
#define SENS_TYPE_BG_HEART_RATE 106

enum sample_shape { SHAPE_ONE, SHAPE_THREE, SHAPE_THREE_RAW, SHAPE_BATTERY };

struct sensor_desc {
	const char *name;
	uint8_t sens_type;	/* written to enable and disable the sensor */
	uint8_t report_type;	/* type the hub tags its samples with */
	enum sample_shape shape;
	uint32_t default_rate;
};

static const struct sensor_desc sensors[] = {
	{ "heart", SENS_TYPE_HEARTRATE_PPG, SENS_TYPE_HEARTRATE_PPG,
	  SHAPE_ONE, SENSOR_RATE_ONCHANGE },
	{ "accel", SENS_TYPE_ACCEL, SENS_TYPE_ACCEL_RAW,
	  SHAPE_THREE_RAW, 50u << RATE_Q10_SHIFT },
	{ "accelerometer", SENS_TYPE_ACCEL, SENS_TYPE_ACCEL_RAW,
	  SHAPE_THREE_RAW, 50u << RATE_Q10_SHIFT },
	{ "gyro", SENS_TYPE_GYRO, SENS_TYPE_GYRO,
	  SHAPE_THREE, 50u << RATE_Q10_SHIFT },
	{ "gyroscope", SENS_TYPE_GYRO, SENS_TYPE_GYRO,
	  SHAPE_THREE, 50u << RATE_Q10_SHIFT },
	{ "baro", SENS_TYPE_BARO, SENS_TYPE_BARO,
	  SHAPE_ONE, 10u << RATE_Q10_SHIFT },
	{ "temp", SENS_TYPE_TEMP, SENS_TYPE_TEMP,
	  SHAPE_ONE, 1u << RATE_Q10_SHIFT },
	{ "als", SENS_TYPE_ALS, SENS_TYPE_ALS,
	  SHAPE_ONE, 5u << RATE_Q10_SHIFT },
	{ "static_ppg", SENS_TYPE_STATIC_PPG, SENS_TYPE_STATIC_PPG,
	  SHAPE_ONE, SENSOR_RATE_ONCHANGE },
	{ "bg_heart", SENS_TYPE_BG_HEART_RATE, SENS_TYPE_BG_HEART_RATE,
	  SHAPE_ONE, SENSOR_RATE_ONCHANGE },
	{ "battery", SENS_TYPE_FUELGAUGE, SENS_TYPE_FUELGAUGE,
	  SHAPE_BATTERY, 1u << RATE_Q10_SHIFT },
};
struct config_cmd {
	uint32_t event_type;
	uint64_t latency_ns;
	uint32_t rate_q10;
	uint8_t sensor_type;
	uint8_t command;
	uint16_t flags;
} __attribute__((packed));

enum config_command { CONFIG_CMD_DISABLE = 0, CONFIG_CMD_ENABLE = 1 };

/* Every sensor event opens with the event id and the batch reference time. */
struct sensor_event_header {
	uint32_t event_type;
	uint64_t reference_time;
} __attribute__((packed));

/*
 * The first sample of a batch overlays the delta-time slot with a descriptor
 * carrying the sample count; later samples carry an encoded delta instead.
 */
struct sensor_first_sample {
	uint8_t num_samples;
	uint8_t bias_fields;
	uint8_t interrupt;
	uint8_t num_flushes;
} __attribute__((packed));

struct triple_axis_sample {
	union {
		uint32_t delta_time;
		struct sensor_first_sample first;
	};
	float x;
	float y;
	float z;
} __attribute__((packed));

/* Same framing as the float form, with 16-bit counts as the payload. */
struct triple_raw_sample {
	union {
		uint32_t delta_time;
		struct sensor_first_sample first;
	};
	int16_t x;
	int16_t y;
	int16_t z;
} __attribute__((packed));

struct single_axis_sample {
	union {
		uint32_t delta_time;
		struct sensor_first_sample first;
	};
	float value;
} __attribute__((packed));

/*
 * A delta is stored shifted unless its low bit marks it as already exact, so
 * the firmware can span a wide range of gaps in 32 bits.
 */
#define DELTA_TIME_EXACT 0x1u
#define DELTA_TIME_SHIFT 9

static volatile sig_atomic_t stop;

static void on_signal(int signo)
{
	(void)signo;
	stop = 1;
}

static uint64_t decode_delta_time(uint32_t delta)
{
	if (delta & DELTA_TIME_EXACT)
		return delta;
	return (uint64_t)delta << DELTA_TIME_SHIFT;
}

static const struct sensor_desc *find_sensor(const char *name)
{
	size_t i;

	for (i = 0; i < sizeof(sensors) / sizeof(sensors[0]); i++)
		if (!strcmp(name, sensors[i].name))
			return &sensors[i];
	return NULL;
}

static int write_config(int fd, uint8_t sensor, enum config_command command,
			uint32_t rate)
{
	struct config_cmd cmd = {
		.event_type = EVT_CONFIG,
		.latency_ns = 0,
		.rate_q10 = command == CONFIG_CMD_ENABLE ? rate : 0,
		.sensor_type = sensor,
		.command = (uint8_t)command,
		.flags = 0,
	};
	ssize_t written = write(fd, &cmd, sizeof(cmd));

	if (written == (ssize_t)sizeof(cmd))
		return 0;
	if (written >= 0)
		errno = EIO;
	return -1;
}

/* JSON has no way to spell NaN or infinity, so an unusable reading is null. */
static void print_number(const char *key, float value, int precision)
{
	if (isfinite(value))
		printf("\"%s\":%.*g", key, precision, (double)value);
	else
		printf("\"%s\":null", key);
}

static void print_sample_prefix(const struct sensor_desc *sensor,
				uint64_t timestamp)
{
	printf("{\"sensor\":\"%s\",\"timestamp_ns\":%llu,", sensor->name,
	       (unsigned long long)timestamp);
}

static void print_packet(const uint8_t *buf, size_t length,
			 const struct sensor_desc *wanted)
{
	struct sensor_event_header header;
	uint64_t timestamp;
	size_t stride, i, count;

	if (length < sizeof(header))
		return;

	memcpy(&header, buf, sizeof(header));
	timestamp = header.reference_time;
	switch (wanted->shape) {
	case SHAPE_THREE:
		stride = sizeof(struct triple_axis_sample);
		break;
	case SHAPE_THREE_RAW:
		stride = sizeof(struct triple_raw_sample);
		break;
	case SHAPE_BATTERY:
		stride = sizeof(struct triple_axis_sample);
		break;
	default:
		stride = sizeof(struct single_axis_sample);
		break;
	}

	if (length < sizeof(header) + stride)
		return;

	/* Every shape opens its first sample with the same descriptor. */
	{
		struct sensor_first_sample first;

		memcpy(&first, buf + sizeof(header), sizeof(first));
		count = first.num_samples;
	}

	for (i = 0; i < count; i++) {
		const uint8_t *raw = buf + sizeof(header) + i * stride;

		if ((size_t)(raw - buf) + stride > length)
			break;

		if (wanted->shape == SHAPE_THREE) {
			struct triple_axis_sample sample;

			memcpy(&sample, raw, sizeof(sample));
			if (i)
				timestamp += decode_delta_time(sample.delta_time);
			print_sample_prefix(wanted, timestamp);
			print_number("x", sample.x, 9);
			putchar(',');
			print_number("y", sample.y, 9);
			putchar(',');
			print_number("z", sample.z, 9);
			puts("}");
		} else if (wanted->shape == SHAPE_BATTERY) {
			struct triple_axis_sample sample;

			memcpy(&sample, raw, sizeof(sample));
			if (i)
				timestamp += decode_delta_time(sample.delta_time);
			print_sample_prefix(wanted, timestamp);
			print_number("percent", sample.x, 4);
			putchar(',');
			print_number("millivolts", sample.y, 6);
			puts("}");
		} else if (wanted->shape == SHAPE_THREE_RAW) {
			struct triple_raw_sample sample;
			const float scale = STANDARD_GRAVITY / ACCEL_COUNTS_PER_G;

			memcpy(&sample, raw, sizeof(sample));
			if (i)
				timestamp += decode_delta_time(sample.delta_time);
			print_sample_prefix(wanted, timestamp);
			print_number("x", sample.x * scale, 6);
			putchar(',');
			print_number("y", sample.y * scale, 6);
			putchar(',');
			print_number("z", sample.z * scale, 6);
			puts("}");
		} else {
			struct single_axis_sample sample;

			memcpy(&sample, raw, sizeof(sample));
			if (i)
				timestamp += decode_delta_time(sample.delta_time);
			print_sample_prefix(wanted, timestamp);
			print_number("value", sample.value, 6);
			puts("}");
		}
	}
	fflush(stdout);
}

/*
 * Print an event exactly as it arrived. Sensor types this tool does not model
 * yet can be inspected this way without guessing at their sample layout.
 */
static void print_raw(const uint8_t *buf, size_t length)
{
	uint32_t event_type;
	size_t i;

	if (length < sizeof(uint32_t))
		return;
	memcpy(&event_type, buf, sizeof(event_type));
	printf("{\"event\":%u,\"raw\":\"", event_type & 0x7fffffffu);
	for (i = 0; i < length; i++)
		printf("%02x", buf[i]);
	puts("\"}");
	fflush(stdout);
}

struct stream {
	const struct sensor_desc *desc;
	uint32_t rate;
};

static const struct sensor_desc *find_by_event(const struct stream *streams,
					       size_t count, uint32_t event_type)
{
	size_t i;

	for (i = 0; i < count; i++)
		if (event_type == EVT_SENSOR_BASE + streams[i].desc->report_type)
			return streams[i].desc;
	return NULL;
}

/*
 * The hub exposes one shared event queue, not a stream per sensor: each event
 * is handed to exactly one reader. Running a process per sensor therefore has
 * them eat each other's samples, so every sensor a caller wants is enabled on
 * a single descriptor here and the results are demultiplexed by event type.
 */
static int stream_sensors(const struct stream *streams, size_t count, bool raw)
{
	uint8_t buf[255];
	struct pollfd pfd[2];
	size_t i;
	int fd, rc = 1;

	fd = open(NANOHUB_DEVICE, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		perror(NANOHUB_DEVICE);
		return 1;
	}
	for (i = 0; i < count; i++) {
		if (write_config(fd, streams[i].desc->sens_type,
				 CONFIG_CMD_ENABLE, streams[i].rate) < 0) {
			perror("enable sensor");
			goto disable;
		}
	}

	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);
	pfd[0].fd = fd;
	pfd[0].events = POLLIN;
	/*
	 * Watch stdin purely for hangup. A sensor that is enabled but quiet
	 * never writes, so it would never see EPIPE; without this the process
	 * outlives its reader and keeps draining the queue from every other
	 * reader on the device.
	 */
	pfd[1].fd = STDIN_FILENO;
	pfd[1].events = 0;
	while (!stop) {
		ssize_t length;

		pfd[0].revents = 0;
		pfd[1].revents = 0;
		if (poll(pfd, 2, 1000) < 0) {
			if (errno == EINTR)
				continue;
			perror("poll");
			goto disable;
		}
		if (pfd[1].revents & (POLLHUP | POLLERR | POLLNVAL)) {
			rc = 0;
			goto disable;
		}
		if (!(pfd[0].revents & POLLIN))
			continue;
		length = read(fd, buf, sizeof(buf));
		if (length < 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			perror("read");
			goto disable;
		}
		if (length <= 0)
			continue;
		if (raw) {
			print_raw(buf, (size_t)length);
		} else if ((size_t)length >= sizeof(uint32_t)) {
			const struct sensor_desc *desc;
			uint32_t event_type;

			memcpy(&event_type, buf, sizeof(event_type));
			desc = find_by_event(streams, count,
					     event_type & 0x7fffffffu);
			if (desc)
				print_packet(buf, (size_t)length, desc);
		}
	}
	rc = 0;

disable:
	/*
	 * A sensor that reports under a different type has to be silenced under
	 * both: disabling only the type we enabled leaves the hub streaming,
	 * which starves every other sensor and outlives this process.
	 */
	for (i = 0; i < count; i++) {
		write_config(fd, streams[i].desc->sens_type,
			     CONFIG_CMD_DISABLE, 0);
		if (streams[i].desc->report_type != streams[i].desc->sens_type)
			write_config(fd, streams[i].desc->report_type,
				     CONFIG_CMD_DISABLE, 0);
	}
	close(fd);
	return rc;
}

static int show_info(void)
{
	static const char *paths[] = {
		"/sys/class/nanohub/nanohub/firmware_version",
		"/sys/class/nanohub/nanohub/app_info",
	};
	char buf[4096];
	size_t i;
	int rc = 0;

	for (i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
		ssize_t length;
		int fd = open(paths[i], O_RDONLY | O_CLOEXEC);

		if (fd < 0) {
			perror(paths[i]);
			rc = 1;
			continue;
		}
		length = read(fd, buf, sizeof(buf) - 1);
		close(fd);
		if (length < 0) {
			perror(paths[i]);
			rc = 1;
			continue;
		}
		buf[length] = '\0';
		printf("%s:\n%s%s", paths[i], buf,
		       length && buf[length - 1] == '\n' ? "" : "\n");
	}
	return rc;
}

static void usage(FILE *out)
{
	size_t i;

	fprintf(out, "usage:\n"
		     "  nanohubctl info\n"
		     "  nanohubctl stream <sensor>[@rate-hz] ...\n"
		     "  nanohubctl dump <sensor>[@rate-hz] ...\n"
		     "\n"
		     "Several sensors may be given; they share one connection\n"
		     "because the hub delivers each event to a single reader.\n"
		     "\nsensors:\n");
	for (i = 0; i < sizeof(sensors) / sizeof(sensors[0]); i++)
		fprintf(out, "  %-14s type %u\n", sensors[i].name,
			sensors[i].sens_type);
}

int main(int argc, char **argv)
{
	struct stream streams[sizeof(sensors) / sizeof(sensors[0])];
	size_t count = 0;
	bool raw;
	int i;

	if (argc == 2 && !strcmp(argv[1], "info"))
		return show_info();
	raw = argc >= 3 && !strcmp(argv[1], "dump");
	if (argc < 3 || (!raw && strcmp(argv[1], "stream"))) {
		usage(stderr);
		return 2;
	}

	for (i = 2; i < argc; i++) {
		char name[32];
		const char *at = strchr(argv[i], '@');
		size_t len = at ? (size_t)(at - argv[i]) : strlen(argv[i]);

		if (count == sizeof(streams) / sizeof(streams[0])) {
			fprintf(stderr, "too many sensors\n");
			return 2;
		}
		if (len >= sizeof(name)) {
			fprintf(stderr, "unknown sensor: %s\n", argv[i]);
			return 2;
		}
		memcpy(name, argv[i], len);
		name[len] = '\0';

		streams[count].desc = find_sensor(name);
		if (!streams[count].desc) {
			fprintf(stderr, "unknown sensor: %s\n", name);
			return 2;
		}
		streams[count].rate = streams[count].desc->default_rate;
		if (at) {
			char *end;
			double hz = strtod(at + 1, &end);

			if (*end || !(hz > 0.0) || hz > MAX_RATE_HZ) {
				fprintf(stderr, "invalid rate: %s\n", at + 1);
				return 2;
			}
			streams[count].rate =
				(uint32_t)(hz * (1 << RATE_Q10_SHIFT) + 0.5);
		}
		count++;
	}
	return stream_sensors(streams, count, raw);
}
