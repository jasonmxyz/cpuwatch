CC = gcc
CFLAGS = -o2
prefix=/usr/bin

.PHONY: clean default install

default: cpuwatch

clean:
	rm cpuwatch

cpuwatch: main.c
	$(CC) $(CFLAGS) -o $@ $^

install: cpuwatch
	install -m 755 -s $^ -t $(prefix)

