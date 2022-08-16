# cpuwatch
A program to report CPU usage

cpuwatch continuously checks `/proc/uptime` to determine the current CPU
utilisation. It writes the utilisation to a file at regular intervals, and can
be configured to measure a moving average of CPU utilisation over some time.

The specified file will contain the current CPU utilisation as a percentage to
one decimal place e.g. `12.3%`. The program continues until it is killed by a
signal, or faults.

## Usage:

```sh
cpuwatch <--output=/path/to/output> <--cpus=N> [options...]
```

### Options:
- `-h`, `--help`:\
  Displays a usage statement.
- `-o FILE`, `--output=FILE`:\
  Write the CPU utilisation to FILE. (__REQUIRED__)
- `-c CPUS`, `--cpus=CPUS`:\
  Assume that there are this number of CPUs installed. (__REQUIRED__)
- `-n SAMPLES`, `--samples=SAMPLES`:\
  Take a moving average of this many intervals. (Default: 1)
- `-i INTERVAL`, `--interval=INTERVAL`:\
  Each sample should be separated by this many seconds. May be a decimal
  number. (Default: 1)

## Example

To update the file `output` every 0.5 seconds with the average CPU utilisation
for the past 2.5 seconds on a 4-core system, run:

```sh
cpuwatch -o output -i0.5 -n5 -c4
```

The utilisation will be averaged over 2.5 seconds because a moving average of 5
intervals is taken.

To update the file `output` every second with the average CPU utilisation for
the previous second on a 4-core system, run:

```sh
cpuwatch -o output -c 4
```

## Building

To build cpuwatch, run:

```sh
make
```

### Build Dependencies

- gcc
- libc
- gzip (for compressing man-page)

## Installing

To install the program, run:

```sh
make install
make install-man
```

This installs cpuwatch to `/usr/bin` and the manual page to `/usr/share/man` by
default, to change the install directories, run:

```sh
make install binprefix=/path/to/install
make install-man manprefix=/path/to/man
```

### Runtime Dependencies

- libc
