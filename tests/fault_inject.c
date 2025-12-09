/* 
 * Fault injection shim for trim_tail tests.
 * Copyright 2025 Yurii Muratov
 * Licensed under the Apache License, Version 2.0 (see LICENSE)
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stddef.h>
#include <sys/types.h>
#include <unistd.h>

// Inject EINTR into pread/pwrite and force a single short write to exercise
// retry loops.

static ssize_t (*real_pread_fn)(int, void *, size_t, off_t);
static ssize_t (*real_pwrite_fn)(int, const void *, size_t, off_t);

static void init_real(void) {
    if (!real_pread_fn) {
        real_pread_fn = (ssize_t (*)(int, void *, size_t, off_t))dlsym(RTLD_NEXT, "pread");
    }
    if (!real_pwrite_fn) {
        real_pwrite_fn = (ssize_t (*)(int, const void *, size_t, off_t))dlsym(RTLD_NEXT, "pwrite");
    }
}

ssize_t pread(int fd, void *buf, size_t count, off_t offset) {
    init_real();

    static int injected_eintr = 0;
    if (!injected_eintr) {
        injected_eintr = 1;
        errno = EINTR;
        return -1;
    }

    return real_pread_fn(fd, buf, count, offset);
}

ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset) {
    init_real();

    static int injected_partial = 0;
    static int injected_eintr = 0;

    if (!injected_partial && count > 1) {
        injected_partial = 1;
        size_t half = count / 2;
        return real_pwrite_fn(fd, buf, half, offset);
    }

    if (!injected_eintr) {
        injected_eintr = 1;
        errno = EINTR;
        return -1;
    }

    return real_pwrite_fn(fd, buf, count, offset);
}

