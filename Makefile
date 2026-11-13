PREFIX ?= $(HOME)/.local
CFLAGS ?= -O2 -Wall -Wextra

fastjump-scan: fastjump-scan.c
	$(CC) $(CFLAGS) -pthread -o $@ $<

install: fastjump-scan
	install -Dm755 fastjump-scan $(PREFIX)/bin/fastjump-scan

clean:
	rm -f fastjump-scan

.PHONY: install clean
