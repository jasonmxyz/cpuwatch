/*
 * A program to report CPU usage.
 *
 * Copyright 2022 Jason Moore <jason@jasonmoore.xyz>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the “Software”), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* Structure to store command line options.
 * Populated in a call to parseCmdLine. */
struct options {
	char *output;
	double interval;
	int ncpu;
	int avg;

	int given_h : 1;
};

int readuptime(double *uptime, double *idletime);
int writeutil(double util, char *path);
int parseCmdLine(int argc, char **argv, struct options *options);
char *argv0;

const char *usage =
"\nusage: cpuwatch <--output=PATH> <--cpus=NUM> [options]\n\n"
"Options:\n"
" -h, --help                 Displays this usage statement.\n"
" -o <PATH>, --output=PATH   The CPU utilisation should be written to PATH.\n"
" -c <NUM>, --cpus=NUM       Number of CPUs on the system.\n"
" -n <NUM>, --samples=NUM    Take a moving average of NUM samples. DEFAULT=1\n"
" -i <NUM>, --interval=NUM   Number of seconds between samples. DEFAULT=1\n"
"\nExamples:\n"
"cpuwatch -o output -i1 -n5 -c4\n"
"  Writes to the file 'output' every 1 second a 5*1 second moving average\n"
"  for a 4-core system.\n"
"cpuwatch -o output -i60 -c12\n"
"  Writes to the file 'output' every 60 seconds the average CPU utilisation\n"
"  for the previous 60 seconds assuming the system has 12 cores.\n\n";

/*
 * Parse the command line and begin reporting CPU utilisation.
 *
 * We determine the system uptime and total CPU idle-time be reading
 * /proc/uptime. The idle-time is divided by the number of CPUs to get an
 * average. Multiple readings are taken to achieve a moving average if
 * required.
 *
 * To calculate the utilisation over a period of time we use the following
 * formula:
 *  u = 100% - (UP - IDLE)
 * When we have two readings separated by some time, we take their difference:
 *  u = 100% - ((NEWUP - OLDUP) / (NEWIDLE - OLDIDLE))
 * As the idle time is given as the sum across all CPUs, we must divide the
 * idle time by the number of CPUs:
 *  u = 100% - ((NEWUP - OLDUP) / ((NEWIDLE - OLDIDLE) / NCPU ))
 *
 * The program continues in a loop until it is stopped by a signal or faults in
 * some way (in which case it exits with code -1).
 */
int main(int argc, char** argv)
{
	/* Parse command line arguments. */
	struct options options;
	if (parseCmdLine(argc, argv, &options) < 0 || options.given_h) {
		fprintf(stderr, "%s", usage);
		return -1;
	}

	/* Save argv[0] as a global variable so it can be used to write error
	 * messages in the above functions. */
	argv0 = argv[0];

	int new = options.avg;
	double times[new + 1][2];

	/* Read the file once and calculate average utilisation so far. */
	if (readuptime(&times[0][0], &times[0][1]) < 0) {
		return -1;
	}
	double u = 100 - 100 * ((times[0][1] / options.ncpu) / times[0][0]);

	/* Pad out the rest of the buffer with copies of the first reading. */
	for (int i = 1; i < new + 1; i++) {
		times[i][0] = times[i-1][0];
		times[i][1] = times[i-1][1];
	}

	/* Continue indefinitely. The program will only terminate if interrupted
	 * with SIGINT/SIGKILL etc, or faults. */
	while (1) {
		/* Write the utilisation to the file. */
		if (writeutil(u, options.output) < 0) {
			return -1;
		}

		/* Wait for some time. */
		if (usleep(options.interval * 1000000) < 0 && errno != EINTR) {
			fprintf(stderr, "%s: Error in usleep (%s)\n",
			        argv[0],
			        strerror(errno));
			return -1;
		}

		/* Shift the values down and read another set of times. */
		for (int i = 0; i < new; i++) {
			times[i][0] = times[i+1][0];
			times[i][1] = times[i+1][1];
		}
		if (readuptime(&times[new][0], &times[new][1]) < 0) {
			return -1;
		}

		/* Perform the calculation again. */
		double uptimediff = times[new][0] - times[0][0];
		double idletimediff = times[new][1] - times[0][1];
		u = 100 - 100 * ((idletimediff / options.ncpu) / uptimediff);
	}

	return 0;
}

/*
 * Read the file /proc/uptime to get the total system uptime and the idle time.
 * Return the two numbers in the given arguments. Close the file so that we can
 * open it again later.
 *
 * On success, 0 is returned, and uptime and idle-time are set to the
 * corresponding values from the file.
 * On failure, -1 is returned, and errno is set to indicate the error.
 */
int readuptime(double *uptime, double *idletime)
{
	FILE *file = fopen("/proc/uptime", "r");
	if (!file) {
		fprintf(stderr, "%s: Could not open /proc/uptime (%s)",
		        argv0, strerror(errno));
		return -1;
	}

	if (fscanf(file, "%lf %lf", uptime, idletime) != 2) {
		fprintf(stderr, "%s: Error scanning /proc/uptime",
		        argv0, strerror(errno));
		return -1;
	}

	fclose(file);
	return 0;
}

/*
 * Write the CPU utilisation to the given file. Truncate the file to length 0
 * before writing ("w" flag). Immediately close the file so that we can be
 * lazy and do this again later.
 *
 * On success, 0 is returned.
 * On failure, -1 is returned, and errno is set to indicate the error.
 */
int writeutil(double util, char *path)
{
	FILE *file = fopen(path, "w");
	if (!file) {
		fprintf(stderr, "%s: Could not open '%s' (%s)",
		        argv0, path, strerror(errno));
		return -1;
	}

	fprintf(file, "%.1f%%", util);

	fclose(file);
	return 0;
}

/*
 * Parse command line arguments using typical syntax and populate the
 * structure with the discovered options.
 *
 * Use the getopt function provided by libc to ensure typical command line
 * behaviour.
 *
 * On success, 0 is returned, and all relevant fields of the options
 * structure are populated according to the command line options.
 * On failure, -1 is returned, errno is set to indicate the error.
 *
 * Errors:
 *     EINVAL: The command line was not properly formatted.
 */
int parseCmdLine(int argc, char **argv, struct options *options)
{
	options->output = NULL;
	options->interval = 1.0;
	options->ncpu = 0;
	options->avg = 1;
	options->given_h = 0;

	/* We record extra data so we can produce better error messages. */
	int given_o = 0;
	int given_i = 0;
	int given_c = 0;
	int given_n = 0;

	int badintervals = 0;
	int badncpus = 0;
	int badavgs = 0;
	char *given_badinterval[argc];
	char *given_badncpu[argc];
	char *given_badavg[argc];

	int nunrecognized = 0;
	int nmissing = 0;
	char *unrecognized[argc];
	for (int i = 0; i < argc; i++) unrecognized[i] = NULL;
	char *missing[argc];

	/* Temporary variables for calculations and such */
	int v;
	double d;
	char *c;

	/* The options we can detect with getopt */
	struct option getopts[6] = {
		{"output", required_argument, 0, 'o'},
		{"interval", required_argument, 0, 'i'},
		{"ncpu", required_argument, 0, 'c'},
		{"samples", required_argument, 0, 'n'},
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0}
	};
	const char *optstring = ":ho:c:n:i:";

	int opt;
	opterr = 0; /* Suppress errors from getopt */

	while ((opt = getopt_long(argc, argv, optstring, getopts, NULL)) != -1) {
	switch (opt) {
	case 'h': /* -h or --help */
		options->given_h++;
		return 0;
	case 'o': /* -o or --output */
		given_o++;
		options->output = optarg;
		break;
	case 'i': /* -i or --interval */
		given_i++;

		v = 0;
		d = 0;
		c = optarg;

		/* Convert the string to a double. */
		for (; *c; c++) {
			if (*c < '0' || *c > '9') {
				if (*c != '.') {
					given_badinterval[badintervals++] = optarg;
				}
				break;
			}
			d = (d * 10) + (*c - '0');
		}
		if (*c == '.') {
			for (c++; *c; c++) {
				if (*c < '0' || *c > '9') {
					given_badinterval[badintervals++] = optarg;
					break;
				}
				d = (d * 10) + (*c - '0');
				v++;
			}
		}
		for (; v; v--) {
			d /= 10;
		}

		options->interval = d;

		break;
	case 'c': /* -c or --cpus */
		given_c++;

		/* Convert the string to an int. */
		v = 0;
		for (char *c = optarg; *c; c++) {
			if (*c < '0' || *c > '9') {
				given_badncpu[badncpus++] = optarg;
				break;
			}
			v = (v * 10) + (*c - '0');
		}
		options->ncpu = v;

		break;
	case 'n': /* -n or --samples */
		given_n++;

		/* Convert the string to an int. */
		v = 0;
		for (char *c = optarg; *c; c++) {
			if (*c < '0' || *c > '9') {
				given_badavg[badavgs++] = optarg;
				break;
			}
			v = (v * 10) + (*c - '0');
		}
		options->avg = v;

		break;
	case '?': /* Unrecognised option */
		nunrecognized++;
		unrecognized[optind - 1] = argv[optind - 1];
		break;
	case ':': /* Option given without an argument. */
		missing[nmissing++] = argv[optind - 1];
		break;
	default: /* Something else. */
		fprintf(stderr, "%s: Unknown error while parsing arguments\n",
		        argv[0]);
		errno = EINVAL;
		return -1;
	}
	}

	int errors = 0;

	/* Output error messages to stderr for each error we detected. */

	if (nunrecognized || nmissing || badintervals || badncpus || given_o > 1 ||
	    given_i > 1 || given_c > 1 || given_n > 1 || given_o == 0 ||
	    given_c == 0)
	{
		fprintf(stderr, "%s: Error(s) processing command line arguments.\n\n",
		        argv[0]);
	}

	if (nunrecognized) {
		fprintf(stderr, "%d option%s not recognised: ",
		        nunrecognized,
		        nunrecognized > 1 ? "s were" : " was");
		int i = 0;
		for (i = 0; i < argc; i++) {
			if (unrecognized[i]) {
				fprintf(stderr, "'%s'",
				        unrecognized[i]);
				i++;
				break;
			}
		}
		for (; i < argc; i++) {
			if (unrecognized[i]) {
				fprintf(stderr, ", '%s'",
				        unrecognized[i]);
				i++;
			}
		}
		fprintf(stderr, "\n");
		errors++;
	}

	if (nmissing) {
		fprintf(stderr, "%d option%s given without an argument: ",
		        nmissing,
		        nmissing > 1 ? "s were" : " was");
		for (int i = 0; i < nmissing; i++) {
			fprintf(stderr, "'%s'%s",
			        missing[i], i + 1 == nmissing ? "\n" : ", ");
		}
		errors++;
	}

	if (given_o > 1) {
		fprintf(stderr, "--output/-o was given %d times (1 maximum).\n",
		        given_o);
		errors++;
	}
	if (given_o == 0) {
		fprintf(stderr, "--output/-o was not given.\n");
		errors++;
	}

	if (given_i > 1) {
		fprintf(stderr, "--interval/-i was given %d times (1 maximum).\n",
		        given_i);
		errors++;
	}

	if (badintervals) {
		fprintf(stderr, "--interval/-i was given improperly %d time%s: ",
		        badintervals,
		        badintervals > 1 ? "s" : "");
		for (int i = 0; i < badintervals; i++) {
			fprintf(stderr, "'%s'%s",
			        given_badinterval[i], i + 1 == badintervals ? "" : ", ");
		}
		fprintf(stderr, ". The interval must be a positive integer value.\n");
		errors++;
	}

	if (given_c > 1) {
		fprintf(stderr, "--cpus/-c was given %d times (1 maximum).\n",
		        given_c);
		errors++;
	}
	if (given_c == 0) {
		fprintf(stderr, "--cpus/-c was not given.\n");
		errors++;
	}

	if (badncpus) {
		fprintf(stderr, "--cpus/-c was given improperly %d time%s: ",
		        badncpus,
		        badncpus > 1 ? "s" : "");
		for (int i = 0; i < badncpus; i++) {
			fprintf(stderr, "'%s'%s",
			        given_badncpu[i], i + 1 == badncpus ? "\n" : ", ");
		}
		errors++;
	}

	if (given_n > 1) {
		fprintf(stderr, "--samples/-n was given %d times (1 maximum).\n",
		        given_n);
		errors++;
	}

	if (badavgs) {
		fprintf(stderr, "--samples/-n was given improperly %d time%s: ",
		        badavgs,
		        badavgs > 1 ? "s" : "");
		for (int i = 0; i < badavgs; i++) {
			fprintf(stderr, "'%s'%s",
			        given_badavg[i], i + 1 == badavgs ? "\n" : ", ");
		}
		errors++;
	}

	/* Return with EINVAL if there were any errors at all. */
	if (errors) {
		errno = EINVAL;
		return -1;
	}

	return 0;
}
