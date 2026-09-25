/*
 * Copyright 2026 Marc Lainez
 * SPDX-License-Identifier: Apache-2.0
 *
 * Small Android/Bionic client for the legacy GPS HAL ABI. This is compiled as
 * a 32-bit ARM PIE and run by the stock /system/bin/linker. It deliberately
 * has only libc.so and libdl.so as direct dependencies; dependencies of the
 * proprietary HAL are resolved from the read-only stock partitions.
 */
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef long long int64_t;
typedef unsigned long size_t;
typedef unsigned long pthread_t;
extern int pthread_create(pthread_t *, const void *, void *(*)(void *), void *);
extern void *malloc(size_t);
extern void free(void *);
extern int snprintf(char *, size_t, const char *, ...);
extern long write(int, const void *, unsigned long);
extern unsigned int sleep(unsigned int);

#define GPS_POSITION_MODE_STANDALONE 0
#define GPS_POSITION_RECURRENCE_PERIODIC 0

struct hw_module_t;
struct hw_device_t;

struct hw_module_methods_t {
    int (*open)(const struct hw_module_t *, const char *, struct hw_device_t **);
};

struct hw_module_t {
    uint32_t tag;
    uint16_t module_api_version;
    uint16_t hal_api_version;
    const char *id;
    const char *name;
    const char *author;
    struct hw_module_methods_t *methods;
    void *dso;
    uint32_t reserved[25];
};

struct hw_device_t {
    uint32_t tag;
    uint32_t version;
    struct hw_module_t *module;
    uint32_t reserved[12];
    int (*close)(struct hw_device_t *);
};

typedef int64_t GpsUtcTime;

typedef struct {
    size_t size;
    uint16_t flags;
    double latitude;
    double longitude;
    double altitude;
    float speed;
    float bearing;
    float accuracy;
    GpsUtcTime timestamp;
} GpsLocation;

typedef struct {
    size_t size;
    uint16_t status;
} GpsStatus;

typedef struct {
    size_t size;
    int num_svs;
    unsigned char opaque[2048];
} GpsSvStatus;

typedef struct {
    size_t size;
    unsigned char opaque[4096];
} GnssSvStatus;

typedef struct {
    size_t size;
    uint16_t year_of_hw;
} GnssSystemInfo;

typedef pthread_t (*gps_create_thread)(const char *, void (*)(void *), void *);

typedef struct {
    size_t size;
    void (*location_cb)(GpsLocation *);
    void (*status_cb)(GpsStatus *);
    void (*sv_status_cb)(GpsSvStatus *);
    void (*nmea_cb)(GpsUtcTime, const char *, int);
    void (*set_capabilities_cb)(uint32_t);
    void (*acquire_wakelock_cb)(void);
    void (*release_wakelock_cb)(void);
    gps_create_thread create_thread_cb;
    void (*request_utc_time_cb)(void);
    void (*set_system_info_cb)(const GnssSystemInfo *);
    void (*gnss_sv_status_cb)(GnssSvStatus *);
} GpsCallbacks;

typedef struct {
    size_t size;
    int (*init)(GpsCallbacks *);
    int (*start)(void);
    int (*stop)(void);
    void (*cleanup)(void);
    int (*inject_time)(GpsUtcTime, int64_t, int);
    int (*inject_location)(double, double, float);
    void (*delete_aiding_data)(uint16_t);
    int (*set_position_mode)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    const void *(*get_extension)(const char *);
} GpsInterface;

struct gps_device_t {
    struct hw_device_t common;
    const GpsInterface *(*get_gps_interface)(struct gps_device_t *);
};

extern const GpsInterface *gps_get_hardware_interface(void);

static void out(const char *s, unsigned long n) { (void)write(1, s, n); }

static unsigned long slen(const char *s)
{
    unsigned long n = 0;
    while (s && s[n]) n++;
    return n;
}

static void event_error(const char *stage, const char *detail)
{
    char b[512];
    int n = snprintf(b, sizeof(b),
        "{\"type\":\"error\",\"stage\":\"%s\",\"detail\":\"%s\"}\n",
        stage, detail ? detail : "unknown");
    if (n > 0) out(b, (unsigned long)n < sizeof(b) ? (unsigned long)n : sizeof(b) - 1);
}

static void location_cb(GpsLocation *l)
{
    char b[512];
    int n;
    if (!l) return;
    n = snprintf(b, sizeof(b),
        "{\"type\":\"location\",\"flags\":%u,\"latitude\":%.9f,"
        "\"longitude\":%.9f,\"altitude_m\":%.3f,\"speed_mps\":%.3f,"
        "\"bearing_deg\":%.3f,\"accuracy_m\":%.3f,\"timestamp_ms\":%lld}\n",
        (unsigned)l->flags, l->latitude, l->longitude, l->altitude,
        (double)l->speed, (double)l->bearing, (double)l->accuracy,
        (long long)l->timestamp);
    if (n > 0) out(b, (unsigned long)n < sizeof(b) ? (unsigned long)n : sizeof(b) - 1);
}

static void status_cb(GpsStatus *s)
{
    char b[96];
    int n = snprintf(b, sizeof(b), "{\"type\":\"status\",\"status\":%u}\n",
                     s ? (unsigned)s->status : 0U);
    if (n > 0) out(b, (unsigned long)n);
}

static void sv_status_cb(GpsSvStatus *s)
{
    char b[96];
    int n = snprintf(b, sizeof(b), "{\"type\":\"satellites\",\"count\":%d}\n",
                     s ? s->num_svs : 0);
    if (n > 0) out(b, (unsigned long)n);
}

static void gnss_sv_status_cb(GnssSvStatus *s)
{
    int count = 0;
    char b[96];
    int n;
    if (s && s->size >= 8) count = *(int *)((unsigned char *)s + 4);
    n = snprintf(b, sizeof(b), "{\"type\":\"satellites\",\"count\":%d}\n", count);
    if (n > 0) out(b, (unsigned long)n);
}

static void nmea_cb(GpsUtcTime timestamp, const char *s, int len)
{
    char b[96];
    int n = snprintf(b, sizeof(b), "{\"type\":\"nmea\",\"timestamp_ms\":%lld,\"sentence\":\"",
                     (long long)timestamp);
    int i;
    if (n > 0) out(b, (unsigned long)n);
    for (i = 0; s && i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\\' || c == '"') { char q[2] = {'\\', (char)c}; out(q, 2); }
        else if (c == '\r') out("\\r", 2);
        else if (c == '\n') out("\\n", 2);
        else if (c >= 0x20) out((const char *)&s[i], 1);
    }
    out("\"}\n", 3);
}

static void capabilities_cb(uint32_t capabilities)
{
    char b[96];
    int n = snprintf(b, sizeof(b), "{\"type\":\"capabilities\",\"mask\":%u}\n", capabilities);
    if (n > 0) out(b, (unsigned long)n);
}

static void noop(void) {}
static void system_info_cb(const GnssSystemInfo *info) { (void)info; }

struct thread_args { void (*start)(void *); void *arg; };
static void *thread_entry(void *p)
{
    struct thread_args *a = (struct thread_args *)p;
    void (*start)(void *) = a->start;
    void *arg = a->arg;
    free(a);
    start(arg);
    return 0;
}

static pthread_t create_thread_cb(const char *name, void (*start)(void *), void *arg)
{
    pthread_t thread = 0;
    struct thread_args *a = (struct thread_args *)malloc(sizeof(*a));
    (void)name;
    if (!a) return 0;
    a->start = start;
    a->arg = arg;
    if (pthread_create(&thread, 0, thread_entry, a) != 0) {
        free(a);
        return 0;
    }
    return thread;
}

static GpsCallbacks callbacks = {
    sizeof(GpsCallbacks), location_cb, status_cb, sv_status_cb, nmea_cb,
    capabilities_cb, noop, noop, create_thread_cb, noop,
    system_info_cb, gnss_sv_status_cb
};

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "/vendor/lib/hw/gps.default.so";
    uint32_t interval = 1000;
    const GpsInterface *gps;
    char ready[160];
    int rc, n;

    (void)slen;
    gps = gps_get_hardware_interface();
    if (!gps || !gps->init || !gps->start) {
        event_error("interface", "legacy GpsInterface unavailable"); return 5;
    }
    rc = gps->init(&callbacks);
    if (rc) { event_error("init", "GPS HAL initialization failed"); return 6; }
    if (gps->set_position_mode)
        (void)gps->set_position_mode(GPS_POSITION_MODE_STANDALONE,
             GPS_POSITION_RECURRENCE_PERIODIC, interval, 0, 0);
    rc = gps->start();
    if (rc) { event_error("start", "GPS HAL failed to start navigation"); return 7; }
    n = snprintf(ready, sizeof(ready),
        "{\"type\":\"ready\",\"hal\":\"%s\",\"interval_ms\":%u}\n", path, interval);
    if (n > 0) out(ready, (unsigned long)n);
    for (;;) sleep(3600);
}

/* Bionic's linker initializes libc before entering this symbol. */
__attribute__((naked, noreturn)) void _start(void)
{
    __asm__ volatile(
        "ldr r0, [sp]\n"
        "add r1, sp, #4\n"
        "bl main\n"
        "mov r7, #1\n"
        "svc #0\n"
        "b .\n");
}
