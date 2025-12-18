/*
 * trim_tail: rewrite a log file in place, keeping only the last N lines.
 * Copyright 2025 Yurii Muratov
 * Licensed under the Apache License, Version 2.0 (see LICENSE)
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <fcntl.h>
#include <limits.h>
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

static off_t find_tail_start_bytes(int fd, off_t size, off_t keep_bytes) {
    if (keep_bytes <= 0) return size;
    if (keep_bytes >= size) return 0;

    off_t target = size - keep_bytes;
    char buf[SCAN_CHUNK];
    off_t pos = target;

    while (pos > 0) {
        size_t chunk = (pos < (off_t)sizeof(buf)) ? (size_t)pos : sizeof(buf);
        pos -= chunk;

        ssize_t r;
        do {
            r = pread(fd, buf, chunk, pos);
        } while (r < 0 && errno == EINTR);

        if (r < 0) return -1;
        if (r == 0) { errno = EIO; return -1; }

        for (ssize_t i = r - 1; i >= 0; --i) {
            if (buf[i] == '\n') {
                off_t newline_pos = pos + i;
                if (newline_pos <= target) {
                    return newline_pos + 1;
                }
            }
        }
    }

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
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s LOGFILE N_LINES            (default: keep lines, positional)\n", prog);
    fprintf(stderr, "  %s LOGFILE --lines|-l N       (explicit lines)\n", prog);
    fprintf(stderr, "  %s LOGFILE --bytes|-b N       (keep bytes, whole lines only)\n", prog);
    fprintf(stderr, "--bytes accepts optional SI suffixes k/m/g (1000-based). Lines and bytes are mutually exclusive.\n");
}

static int print_version(void) {
    printf("trim_tail %s\n", VERSION);
    printf("Copyright 2025 Yurii Muratov\n");
    printf("License Apache-2.0 (see LICENSE)\n");
    return 0;
}

static int parse_lines_arg(const char *arg, long *out) {
    char *endptr = NULL;
    errno = 0;
    long val = strtol(arg, &endptr, 10);
    if (endptr == arg || *endptr != '\0' || errno == ERANGE) {
        return -1;
    }
    *out = val;
    return 0;
}

static int parse_bytes_arg(const char *arg, long *out) {
    char *endptr = NULL;
    errno = 0;
    long long base = strtoll(arg, &endptr, 10);
    if (endptr == arg || errno == ERANGE) {
        return -1;
    }

    long long multiplier = 1;
    if (*endptr != '\0') {
        if (*(endptr + 1) != '\0') {
            return -1; // only single-character suffix allowed
        }
        switch (tolower((unsigned char)*endptr)) {
            case 'k':
                multiplier = 1000LL;
                break;
            case 'm':
                multiplier = 1000LL * 1000LL;
                break;
            case 'g':
                multiplier = 1000LL * 1000LL * 1000LL;
                break;
            default:
                return -1;
        }
    }

    long long val = base * multiplier;
    if (multiplier != 0 && val / multiplier != base) {
        return -1; // overflow
    }
    if (val < 0 || val > LONG_MAX) {
        return -1;
    }

    *out = (long)val;
    return 0;
}

int main(int argc, char **argv) {
    static const struct option long_opts[] = {
        {"lines", required_argument, NULL, 'l'},
        {"bytes", required_argument, NULL, 'b'},
        {"version", no_argument, NULL, 'V'},
        {"help", no_argument, NULL, 'h'},
        {0, 0, 0, 0}
    };

    long keep_lines = -1;
    long keep_bytes = -1;
    int lines_set = 0;
    int bytes_set = 0;

    int opt;
    while ((opt = getopt_long(argc, argv, "l:b:vVh", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'l':
                if (lines_set) {
                    fprintf(stderr, "--lines specified multiple times\n");
                    return 1;
                }
                if (parse_lines_arg(optarg, &keep_lines) < 0) {
                    fprintf(stderr, "--lines requires an integer value\n");
                    return 1;
                }
                lines_set = 1;
                break;
            case 'b':
                if (bytes_set) {
                    fprintf(stderr, "--bytes specified multiple times\n");
                    return 1;
                }
                if (parse_bytes_arg(optarg, &keep_bytes) < 0) {
                    fprintf(stderr, "--bytes requires an integer value (optional k/m/g suffix)\n");
                    return 1;
                }
                bytes_set = 1;
                break;
            case 'v':
            case 'V':
                return print_version();
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    if (optind >= argc) {
        print_usage(argv[0]);
        return 1;
    }

    const char *path = argv[optind++];

    long positional_lines = -1;
    if (optind < argc) {
        if (optind + 1 < argc) {
            print_usage(argv[0]);
            return 1;
        }
        if (parse_lines_arg(argv[optind], &positional_lines) < 0) {
            fprintf(stderr, "N_LINES must be an integer\n");
            return 1;
        }
    }

    if (lines_set && bytes_set) {
        fprintf(stderr, "Specify either --lines or --bytes, not both\n");
        return 1;
    }

    if (bytes_set && positional_lines >= 0) {
        fprintf(stderr, "Positional N_LINES is not allowed with --bytes\n");
        return 1;
    }

    if (lines_set && positional_lines >= 0) {
        fprintf(stderr, "Provide line count either positionally or via --lines, not both\n");
        return 1;
    }

    if (!bytes_set && !lines_set && positional_lines < 0) {
        fprintf(stderr, "Line count required (positional N_LINES or --lines)\n");
        print_usage(argv[0]);
        return 1;
    }

    if (!bytes_set) {
        if (lines_set && keep_lines < 0) keep_lines = 0;
        if (!lines_set && positional_lines >= 0) keep_lines = positional_lines;
        lines_set = 1; // ensure we treat mode as lines
    }

    if (bytes_set && keep_bytes < 0) keep_bytes = 0;

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

    if (lines_set) {
        if (keep_lines <= 0) {
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

        off_t start = find_tail_start(fd, size, keep_lines, ends_with_newline);
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
    } else { // bytes_set
        if (keep_bytes <= 0) {
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

        if ((off_t)keep_bytes >= size) {
            flock(fd, LOCK_UN);
            close(fd);
            return 0; // no-op
        }

        off_t start = find_tail_start_bytes(fd, size, (off_t)keep_bytes);
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
