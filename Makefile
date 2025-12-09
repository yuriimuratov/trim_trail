VERSION_TXT := $(shell cat VERSION)
CC ?= cc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra
LDFLAGS ?=
STRIP ?= strip
BINARY := trim_tail

all: $(BINARY)

$(BINARY): trim_tail.c version.h
	$(CC) $(CFLAGS) trim_tail.c -o $(BINARY) $(LDFLAGS)
	$(STRIP) $(BINARY)

test: $(BINARY) tests/run_tests.sh
	./tests/run_tests.sh

version.h: VERSION FORCE
	@base="$(VERSION_TXT)"; \
	if git -C "$(PWD)" describe --tags --exact-match --quiet >/dev/null 2>&1; then \
		ver="$$base"; \
	else \
		hash=$$(git -C "$(PWD)" rev-parse --short HEAD 2>/dev/null || true); \
		if [ -n "$$hash" ]; then ver="$$base-$$hash"; else ver="$$base"; fi; \
	fi; \
	tmp=$$(mktemp) && \
	printf '#ifndef VERSION\n#define VERSION "%s"\n#endif\n' "$$ver" > $$tmp && \
	if ! cmp -s $$tmp version.h; then mv $$tmp version.h; else rm -f $$tmp; fi

install: $(BINARY)
	install -m 0755 $(BINARY) /usr/local/bin/$(BINARY)

uninstall:
	rm -f /usr/local/bin/$(BINARY)

clean:
	rm -f $(BINARY) version.h

.PHONY: all test install uninstall clean FORCE

FORCE:
