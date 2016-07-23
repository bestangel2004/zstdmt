
/**
 * Copyright © 2016 Tino Reichardt
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License Version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>
#include <zbuff.h>

#include "zstdmt.h"
#include "util.h"

/* program for testing threaded stuff on zstd */

static void perror_exit(const char *msg)
{
	perror(msg);

	exit(1);
}

static void version(void)
{
	printf("zstd-mt version 0.1\n");

	exit(0);
}

static void usage(void)
{
	printf("Usage: zstd-mt [options] infile outfile\n\n");
	printf("Otions:\n");
	printf(" -l N    set level of compression (default: 3)\n");
	printf(" -t N    set number of threads (default: 2)\n");
	printf(" -i N    set number of iterations for testing (default: 1)\n");
	printf(" -c      compress (default mode)\n");
	printf(" -d      use decompress mode\n");
	printf(" -h      show usage\n");
	printf(" -v      show version\n");

	exit(0);
}

#define MODE_COMPRESS    1
#define MODE_DECOMPRESS  2

/* for the -i option */
#define MAX_ITERATIONS   60000

static void do_compress(int threads, int level, int fdin, int fdout)
{
	/* 1) create compression context */
	ZSTDMT_CCtx *ctx = ZSTDMT_createCCtx(threads, level);
	if (!ctx)
		perror_exit("Allocating ctx failed!");

	/* 2) get pointer for input buffer, this is constant */
	void *inbuf = ZSTDMT_GetInBufferCCtx(ctx);
	if (!inbuf)
		perror_exit("Input buffer has Zero Size?!");

	/* 3) get optimal size for the input data */
	int insize = ZSTDMT_GetInSizeCCtx(ctx);
	int t;

	for (;;) {
		/* 4) read input */
		ssize_t ret = read_loop(fdin, inbuf, insize);
		if (ret == 0)
			break;

		/* 5) start threaded compression */
		ZSTDMT_CompressCCtx(ctx, ret);

		for (t = 0; t < threads; t++) {
			void *outbuf;
			size_t len;

			/**
			 * 6) read the compressed data and write them
			 * -> the order is important here!
			 */
			outbuf = ZSTDMT_GetCompressedCCtx(ctx, t, &len);
			if (len == 0)
				break;

			/* write data */
			ret = write_loop(fdout, outbuf, len);
		}
	}

	printf("insize=%zu outsize=%zu frames=%zu\n",
	       ZSTDMT_GetCurrentInsizeCCtx(ctx),
	       ZSTDMT_GetCurrentOutsizeCCtx(ctx),
	       ZSTDMT_GetCurrentFrameCCtx(ctx));

	ZSTDMT_freeCCtx(ctx);
}

static void do_decompress(int threads, int fdin, int fdout)
{
	unsigned char buf[4];

	ZSTDMT_DCtx *ctx = ZSTDMT_createDCtx(threads);
	if (!ctx)
		perror_exit("Allocating ctx failed!");

	/* 2) get pointer for input buffer, this is constant */
	void *inbuf = ZSTDMT_GetInBufferDCtx(ctx);
	if (!inbuf)
		perror_exit("Input buffer has Zero Size?!");

	ZSTDMT_freeDCtx(ctx);
}

int main(int argc, char **argv)
{
	/* default options: */
	int opt, opt_threads = 2, opt_level = 3;
	int opt_mode = MODE_COMPRESS, fdin, fdout;
	int opt_iterations = 1;

	while ((opt = getopt(argc, argv, "vhl:t:i:dc")) != -1) {
		switch (opt) {
		case 'v':	/* version */
			version();
		case 'h':	/* help */
			usage();
		case 'l':	/* level */
			opt_level = atoi(optarg);
			break;
		case 't':	/* threads */
			opt_threads = atoi(optarg);
			break;
		case 'i':	/* iterations */
			opt_iterations = atoi(optarg);
			break;
		case 'd':	/* mode = decompress */
			opt_mode = MODE_DECOMPRESS;
			break;
		case 'c':	/* mode = compress */
			opt_mode = MODE_COMPRESS;
			break;
		default:
			usage();
		}
	}

	/* prog [options] infile outfile */
	if (argc != optind + 2)
		usage();

	/**
	 * check parameters
	 */

	/* opt_level = 1..22 */
	if (opt_level < 1)
		opt_level = 1;
	else if (opt_level > ZSTD_maxCLevel())
		opt_level = ZSTD_maxCLevel();

	/* opt_threads = 1..ZSTDMT_THREADMAX */
	if (opt_threads < 1)
		opt_threads = 1;
	else if (opt_threads > ZSTDMT_THREADMAX)
		opt_threads = ZSTDMT_THREADMAX;

	/* opt_iterations = 1..MAX_ITERATIONS */
	if (opt_iterations < 1)
		opt_iterations = 1;
	else if (opt_iterations > MAX_ITERATIONS)
		opt_iterations = MAX_ITERATIONS;

	/* file names */
	fdin = open_read(argv[optind]);
	if (fdin == -1)
		perror_exit("Opening infile failed");

	fdout = open_rw(argv[optind + 1]);
	if (fdout == -1)
		perror_exit("Opening outfile failed");

	for (;;) {
		if (opt_mode == MODE_COMPRESS) {
			do_compress(opt_threads, opt_level, fdin, fdout);
		} else {
			do_decompress(opt_threads, fdin, fdout);
		}

		opt_iterations--;
		if (opt_iterations == 0)
			break;

		lseek(fdin, 0, SEEK_SET);
		lseek(fdout, 0, SEEK_SET);
	}

	/* exit should flush stdout */
	exit(0);
}
