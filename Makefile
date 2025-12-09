VERSION_TXT := $(shell cat VERSION)
CC ?= cc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra
LDFLAGS ?=
BINARY := trim_tail

all: $(BINARY)

$(BINARY): trim_tail.c version.h
	$(CC) $(CFLAGS) trim_tail.c -o $(BINARY) $(LDFLAGS)

test: $(BINARY) tests/run_tests.sh
	./tests/run_tests.sh

version.h: VERSION
	printf '#ifndef VERSION\n#define VERSION "%s"\n#endif\n' "$(VERSION_TXT)" > version.h

install: $(BINARY)
	install -m 0755 $(BINARY) /usr/local/bin/$(BINARY)

uninstall:
	rm -f /usr/local/bin/$(BINARY)

clean:
	rm -f $(BINARY) version.h

.PHONY: all test install uninstall clean
