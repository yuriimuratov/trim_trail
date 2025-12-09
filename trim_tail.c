// trim_tail: rewrite a log file in place, keeping only the last N lines
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "version.h"

#ifndef VERSION
#define VERSION "dev"
#endif

#define SCAN_CHUNK (64 * 1024)
#define COPY_CHUNK (128 * 1024)

static off_t find_tail_start(int fd, off_t size, long keep_lines, int ends_with_newline) {
    if (keep_lines <= 0) return size; // drop everything

    char buf[SCAN_CHUNK];
    off_t pos = size;
    long lines = ends_with_newline ? 0 : 1; // count final partial line if no trailing NL

    while (pos > 0) {
        size_t chunk = (pos < (off_t)sizeof(buf)) ? (size_t)pos : sizeof(buf);
        pos -= chunk;

        ssize_t r;
        do {
            r = pread(fd, buf, chunk, pos);
        } while (r < 0 && errno == EINTR);

        if (r < 0) return -1; // propagate read errors
        if (r == 0) { errno = EIO; return -1; } // unexpected EOF

        for (ssize_t i = r - 1; i >= 0; --i) {
            if (buf[i] == '\n' && ++lines > keep_lines) {
                return pos + i + 1; // start offset of last keep_lines
            }
        }
    }
    // File has <= keep_lines newlines; keep whole file
    return 0;
}

static int copy_tail(int fd, off_t src_off, off_t size) {
    char buf[COPY_CHUNK];
    off_t dst_off = 0;

    while (src_off < size) {
        size_t to_read = (size - src_off > (off_t)sizeof(buf)) ? sizeof(buf) : (size_t)(size - src_off);
        ssize_t r;
        do {
            r = pread(fd, buf, to_read, src_off);
        } while (r < 0 && errno == EINTR);
        if (r < 0) return -1;
        if (r == 0) { errno = EIO; return -1; }

        size_t read_total = (size_t)r;
        size_t written = 0;
        while (written < read_total) {
            ssize_t w;
            do {
                w = pwrite(fd, buf + written, read_total - written, dst_off + written);
            } while (w < 0 && errno == EINTR);
            if (w < 0) return -1;
            if (w == 0) { errno = EIO; return -1; }
            written += (size_t)w;
        }

        src_off += (off_t)read_total;
        dst_off += (off_t)read_total;
    }

    return ftruncate(fd, dst_off);
}

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s LOGFILE N_LINES\n", prog);
}

static int print_version(void) {
    printf("trim_tail %s\n", VERSION);
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && (!strcmp(argv[1], "--version") || !strcmp(argv[1], "-V") || !strcmp(argv[1], "-v"))) {
        return print_version();
    }

    if (argc != 3) {
        print_usage(argv[0]);
        return 1;
    }

    const char *path = argv[1];
    char *endptr = NULL;
    long keep = strtol(argv[2], &endptr, 10);
    if (endptr == argv[2] || *endptr != '\0') {
        fprintf(stderr, "N_LINES must be an integer\n");
        return 1;
    }
    if (keep < 0) keep = 0;

    int fd = open(path, O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    if (flock(fd, LOCK_EX) < 0) {
        perror("flock");
        close(fd);
        return 1;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        perror("fstat");
        flock(fd, LOCK_UN);
        close(fd);
        return 1;
    }

    off_t size = st.st_size;

    if (keep <= 0) {
        if (ftruncate(fd, 0) < 0) {
            perror("ftruncate");
            flock(fd, LOCK_UN);
            close(fd);
            return 1;
        }
        if (fsync(fd) < 0) {
            perror("fsync");
            flock(fd, LOCK_UN);
            close(fd);
            return 1;
        }
        flock(fd, LOCK_UN);
        close(fd);
        return 0;
    }

    int ends_with_newline = 0;
    if (size > 0) {
        char last;
        ssize_t r;
        do {
            r = pread(fd, &last, 1, size - 1);
        } while (r < 0 && errno == EINTR);

        if (r < 0) {
            perror("pread");
            flock(fd, LOCK_UN);
            close(fd);
            return 1;
        }

        if (r == 1 && last == '\n') ends_with_newline = 1;
    }

    off_t start = find_tail_start(fd, size, keep, ends_with_newline);
    if (start < 0) {
        perror("pread");
        flock(fd, LOCK_UN);
        close(fd);
        return 1;
    }

    if (start > 0 && start < size) {
        if (copy_tail(fd, start, size) < 0) {
            perror("copy/truncate");
            flock(fd, LOCK_UN);
            close(fd);
            return 1;
        }
    }

    if (fsync(fd) < 0) {
        perror("fsync");
        flock(fd, LOCK_UN);
        close(fd);
        return 1;
    }

    flock(fd, LOCK_UN);
    close(fd);
    return 0;
}
