/*
 * Copyright 2026 Marc Lainez
 * SPDX-License-Identifier: Apache-2.0
 *
 * Reproduce Android init's `socket gps stream ...` contract for gpsd/glgps.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    struct sockaddr_un addr;
    int fd;

    if (argc < 2) {
        fprintf(stderr, "usage: android-socket-exec PROGRAM [ARGS...]\n");
        return 2;
    }

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return 3;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, "/dev/socket/gps");
    unlink(addr.sun_path);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
        chmod(addr.sun_path, 0660) < 0 || listen(fd, 4) < 0) {
        perror("gps socket");
        return 5;
    }

    if (fd != 3) {
        if (dup2(fd, 3) < 0) {
            perror("dup2");
            return 6;
        }
        close(fd);
    }
    if (fcntl(3, F_SETFD, 0) < 0 || setenv("ANDROID_SOCKET_gps", "3", 1) < 0) {
        perror("socket environment");
        return 7;
    }

    execv(argv[1], &argv[1]);
    fprintf(stderr, "exec %s: %s\n", argv[1], strerror(errno));
    return 8;
}

