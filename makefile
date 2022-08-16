CC = gcc
CFLAGS = -o2
binprefix=/usr/bin
manprefix=/usr/share/man

.PHONY: clean default install

default: cpuwatch

clean:
	rm -f cpuwatch
	rm -f cpuwatch.1.gz

cpuwatch: main.c
	$(CC) $(CFLAGS) -o $@ $^

%.gz: %
	gzip -k $^

install: cpuwatch
	install -m 755 -s $^ -t $(binprefix)

install-man: cpuwatch.1.gz
	install -m 755 $^ -t $(manprefix)/man1

