#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "bseq.h"
#include "minigraph.h"
#include "mgpriv.h"
#include "ketopt.h"

#ifdef __linux__
#include <sys/resource.h>
#include <sys/time.h>
void liftrlimit()
{
	struct rlimit r;
	getrlimit(RLIMIT_AS, &r);
	r.rlim_cur = r.rlim_max;
	setrlimit(RLIMIT_AS, &r);
}
#else
void liftrlimit() {}
#endif

static ko_longopt_t long_options[] = {
	{ "print-gfa",    ko_no_argument,       301 },
	{ 0, 0, 0 }
};

static inline int64_t mg_parse_num(const char *str)
{
	double x;
	char *p;
	x = strtod(str, &p);
	if (*p == 'G' || *p == 'g') x *= 1e9;
	else if (*p == 'M' || *p == 'm') x *= 1e6;
	else if (*p == 'K' || *p == 'k') x *= 1e3;
	return (int64_t)(x + .499);
}

int main(int argc, char *argv[])
{
	const char *opt_str = "x:k:w:t:r:";
	ketopt_t o = KETOPT_INIT;
	mg_mapopt_t opt;
	mg_idxopt_t ipt;
	int i, c, n_threads = 4, print_gfa = 0;
//	char *fnw = 0, *rg = 0, *s;
	FILE *fp_help = stderr;
	mg_idx_t *gi;

	mg_verbose = 3;
	liftrlimit();
	mg_realtime0 = realtime();
	mg_opt_set(0, &ipt, &opt);

	while ((c = ketopt(&o, argc, argv, 1, opt_str, long_options)) >= 0) { // test command line options and apply option -x/preset first
		if (c == 'x') {
			if (mg_opt_set(o.arg, &ipt, &opt) < 0) {
				fprintf(stderr, "[ERROR] unknown preset '%s'\n", o.arg);
				return 1;
			}
		} else if (c == ':') {
			fprintf(stderr, "[ERROR] missing option argument\n");
			return 1;
		} else if (c == '?') {
			fprintf(stderr, "[ERROR] unknown option in \"%s\"\n", argv[o.i - 1]);
			return 1;
		}
	}
	o = KETOPT_INIT;

	while ((c = ketopt(&o, argc, argv, 1, opt_str, long_options)) >= 0) {
		if (c == 'w') ipt.w = atoi(o.arg);
		else if (c == 'k') ipt.k = atoi(o.arg);
		else if (c == 't') n_threads = atoi(o.arg);
		else if (c == 'r') opt.bw = mg_parse_num(o.arg);
		else if (c == 301) print_gfa = 1;
	}
	if (mg_opt_check(&ipt, &opt) < 0)
		return 1;

	if (argc == o.ind || fp_help == stdout) {
		fprintf(fp_help, "Usage: minigraph [options] <target.fa> [query.fa] [...]\n");
		fprintf(fp_help, "Options:\n");
		fprintf(fp_help, "  Indexing:\n");
		fprintf(fp_help, "    -k INT       k-mer size (no larger than 28) [%d]\n", ipt.k);
		fprintf(fp_help, "    -w INT       minizer window size [%d]\n", ipt.w);
		fprintf(fp_help, "  Mapping:\n");
		fprintf(fp_help, "    -r NUM       bandwidth used in chaining and DP-based alignment [%d]\n", opt.bw);
		fprintf(fp_help, "  Input/output:\n");
		fprintf(fp_help, "    -t INT       number of threads [%d]\n", n_threads);
		return fp_help == stdout? 0 : 1;
	}

	gi = mg_index_file(argv[o.ind], ipt.k, ipt.w, ipt.bucket_bits, ipt.flag, n_threads);
	if (gi == 0) {
		fprintf(stderr, "[ERROR] failed to load the graph from file '%s'\n", argv[o.ind]);
		return 1;
	}
	if (print_gfa) {
		gfa_print(gi->g, stdout, 1);
		goto free_gfa;
	}

#if 1
	int sid = gfa_name2id(gi->g, "MTh0");
	gfa_sub_t *sub;
	if (sid < 0) abort();
	sub = gfa_sub_from(0, gi->g, sid<<1|0, 5000);
	gfa_sub_print(stdout, gi->g, sub);
	gfa_sub_destroy(sub);
#endif

	for (i = o.ind + 1; i < argc; ++i)
		mg_map_file(gi, argv[i], &opt, n_threads);

free_gfa:
	mg_idx_destroy(gi);

	if (fflush(stdout) == EOF) {
		fprintf(stderr, "[ERROR] failed to write the results\n");
		exit(EXIT_FAILURE);
	}

	if (mg_verbose >= 3) {
		fprintf(stderr, "[M::%s] Version: %s\n", __func__, MG_VERSION);
		fprintf(stderr, "[M::%s] CMD:", __func__);
		for (i = 0; i < argc; ++i)
			fprintf(stderr, " %s", argv[i]);
		fprintf(stderr, "\n[M::%s] Real time: %.3f sec; CPU: %.3f sec; Peak RSS: %.3f GB\n", __func__, realtime() - mg_realtime0, cputime(), peakrss() / 1024.0 / 1024.0 / 1024.0);
	}
	return 0;
}