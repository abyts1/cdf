PREFIX ?= $(HOME)/.local
CFLAGS ?= -O2 -Wall -Wextra

cdf-scan: cdf-scan.c
	$(CC) $(CFLAGS) -pthread -o $@ $<

install: cdf-scan
	install -Dm755 cdf-scan $(PREFIX)/bin/cdf-scan

clean:
	rm -f cdf-scan

.PHONY: install clean
