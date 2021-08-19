#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <assert.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/types.h>

#include <perfmon/pfmlib_perf_event.h>

struct event_data {
	unsigned long last;
	unsigned long total;
	unsigned long min;
	unsigned long max;
};

static const char *evname[] = {
	"page-faults",
//	"cpu-cycles",
//	"instructions",
//	"cache-misses",
//	"dTLB-load-misses",
};

static const char *pgflag[] = {
	"LOCKED",
	"ERROR",
	"REFERENCED",
	"UPTODATE",
	"DIRTY",
	"LRU",
	"ACTIVE",
	"SLAB",
	"WRITEBACK",
	"RECLAIM",
	"BUDDY",
	"MMAP",
	"ANON",
	"SWAPCACHE",
	"SWAPBACKED",
	"COMPOUND-HEAD",
	"COMPOUND-TAIL",
	"HUGE",
	"UNEVICTABLE",
	"HWPOISON",
	"NOPAGE",
	"KSM",
	"THP",
	"BALLOON",
	"ZERO-PAGE",
	"IDLE",
	"RESERVED",
	"MLOCKED",
	"MAPPEDTODISK",
	"PRIVATE",
	"PRIVATE-2",
	"OWNER-PRIVATE",
	"ARCH",
	"UNCACHED",
	"SOFTDIRTY",
};

static inline
void perf_event_reset(int fd)
{
	assert(!ioctl(fd, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP));
}

static inline
void perf_event_enable(int fd)
{
	assert(!ioctl(fd, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP));
}

static inline
void perf_event_disable(int fd)
{
	assert(!ioctl(fd, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP));
}

static inline
void perf_event_read(int fd, struct event_data *evcount)
{
	assert(read(fd, &evcount->last, sizeof(unsigned long)) > 0);
	evcount->total += evcount->last;
	evcount->min = MIN(evcount->min, evcount->last);
	evcount->max = MAX(evcount->max, evcount->last);
}

static
int perf_event_prepare(const char *name, int level, int group)
{
	struct perf_event_attr attr;
	int fd;

	memset(&attr, 0, sizeof(struct perf_event_attr));
	assert(pfm_get_perf_event_encoding(name, level, &attr,
					   NULL, NULL) == PFM_SUCCESS);
	attr.disabled = 1;
	assert((fd = perf_event_open(&attr, 0, -1, group, 0)) > 0);
	return fd;
}

static inline __attribute__((always_inline))
void loadflush(volatile void *addr)
{
	register volatile unsigned long tmp asm("r14");
#ifdef __x86_64__
	asm volatile("mov %1, %0; clflush (%1); sfence" : "=r"(tmp) : "r"(addr) : "memory");
#elif defined(__powerpc64__)
	asm volatile("ld %0, 0(%1); dcbf 0, %1; sync" : "=r"(tmp) : "r"(addr) : "memory");
#endif
}

static inline
unsigned long bits(unsigned long val, unsigned int l, unsigned int r)
{
	unsigned long bits, mask;
	bits = l - r + 1;
	mask = (bits == (sizeof(unsigned long) * 8)) ? -1UL : ((1UL << bits) - 1);
	return (val >> r) & mask;
}

static
int gethugepagesize(void)
{
	int hpgsize;
	FILE *fp;

	fp = popen("cat /proc/meminfo | grep Hugepagesize: | awk '{ print $2}'", "r");
	assert(fp && fscanf(fp, "%d", &hpgsize) > 0);
	pclose(fp);

	return hpgsize * 1024;
}

static
unsigned long getpagedata(const char *path, void *addr)
{
	unsigned long entry, offset;
	int fd;

	fd = open(path, O_RDONLY);
	offset = ((unsigned long) addr / getpagesize()) * 8;
	assert(fd && pread(fd, &entry, sizeof(unsigned long), offset) > 0);
	close(fd);

	return entry;
}

static
void pageinfo(void *addr)
{
	unsigned long paddr, pgmap, pgcount, pgshift, pgflags;
	unsigned int i;

	pgmap = getpagedata("/proc/self/pagemap", addr);
	pgshift = 63 - __builtin_clzl(getpagesize());
	paddr = bits(pgmap, 54, 0) << pgshift;
	pgcount = getpagedata("/proc/kpagecount", (void *) paddr);
	pgflags = getpagedata("/proc/kpageflags", (void *) paddr);

	printf("page information for virtual addr %16p\n", addr);
	printf("  page frame number (if present)   = 0x%016lx\n", bits(pgmap, 54, 0));
	printf("  swap type (if swapped)           = 0x%016lx\n", bits(pgmap, 4, 0));
	printf("  swap offset (if swapped)         = 0x%016lx\n", bits(pgmap, 54, 5));
	printf("  pte is soft-dirty                = 0x%016lx\n", bits(pgmap, 55, 55));
	printf("  page exclusively mapped          = 0x%016lx\n", bits(pgmap, 56, 56));
	printf("  page is file-page or shared-anon = 0x%016lx\n", bits(pgmap, 61, 61));
	printf("  page swapped                     = 0x%016lx\n", bits(pgmap, 62, 62));
	printf("  page present                     = 0x%016lx\n", bits(pgmap, 63, 63));
	printf("  page count                       = 0x%016lx\n", pgcount);
	printf("  page flags                       = 0x%016lx [", pgflags);
	for (i = 0; i < sizeof(pgflag) / sizeof(char *); i++)
		if (pgflags & (1UL << ((i > 25) ? i + 6 : i)))
			printf(" %s", pgflag[i]);
	printf(" ]\n");
}

static
void usage(const char *prog)
{
	fprintf(stderr, "Usage: %s [-H] [-L] [-p <level(s)>]\n", prog);
	fprintf(stderr, "  -H		use hugepage mapping\n");
	fprintf(stderr, "  -P		use pre-faulted mapping\n");
	fprintf(stderr, "  -p <0..3>	measure at one or more privilege levels\n");
	fprintf(stderr, "  		level 0 usually corresponds to kernel level\n");
	fprintf(stderr, "  		level 3 usually corresponds to user level (default)\n");
	fprintf(stderr, "  -h, -?	print this help message\n");
}

int main(int argc, char **argv)
{
	unsigned long pgsize, hpgsize, pgprot, claddr, clsize, clcount, mflags;
	char *tokens[] = { "0", "1", "2", "3", NULL };
	int *fd, nevents, plm, idx, opt, i, j;
	volatile unsigned int *addr, npages;
	struct event_data *evcount;
	char *subopts, *value;

	plm = PFM_PLM3;
	mflags = MAP_PRIVATE | MAP_ANONYMOUS;

	while ((opt = getopt(argc, argv, "HPp:h?")) != -1) {
		switch (opt) {
			case 'H':
				mflags |= MAP_HUGETLB;
				break;
			case 'P':
				mflags |= MAP_POPULATE;
				break;
			case 'p':
				subopts = optarg;
				while (*subopts != '\0') {
					switch (getsubopt(&subopts, tokens, &value)) {
						case 0:
							plm |= PFM_PLM0;
							break;
						case 1:
							plm |= PFM_PLM1;
							break;
						case 2:
							plm |= PFM_PLM2;
							break;
						case 3:
							plm |= PFM_PLM3;
							break;
						default:
							usage(argv[0]);
							exit(EXIT_FAILURE);
					}
				}
				break;
			case 'h':
			case '?':
			default:
				usage(argv[0]);
				exit(EXIT_FAILURE);
		}
	}

	pfm_initialize();
	pgsize = getpagesize();
	hpgsize = gethugepagesize();
	npages = hpgsize / pgsize;
	pgprot = PROT_READ | PROT_WRITE;
	clsize = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
//	clcount = npages * pgsize / clsize;
	clcount = 1;
	nevents = sizeof(evname) / sizeof(char *);
	printf("pid = %d, page size = %lu, hugepage size = %lu, "
	       "L1 dcache line size = %lu, events = %d\n",
	       getpid(), pgsize, hpgsize, clsize, nevents);

	fd = (int *) malloc(nevents * sizeof(int));
	evcount = (struct event_data *) malloc(nevents * sizeof(struct event_data));
	assert(fd != NULL && evcount != NULL);

	memset(evcount, 0, nevents * sizeof(evcount[i]));
	for (i = 0; i < nevents; i++) {
		fd[i] = perf_event_prepare(evname[i], plm, i > 0 ? fd[0] : -1);
		evcount[i].min = ULONG_MAX;
		evcount[i].max = 0;
	}

	addr = (unsigned int *) mmap(NULL, npages * pgsize, pgprot, mflags, -1, 0);
	assert(addr != MAP_FAILED);
	pageinfo((void *) addr);

	for (i = 0; i < clcount; i++) {
		idx = i * clsize / sizeof(unsigned int);
		claddr = ((unsigned long) &addr[idx]) & ~(clsize - 1UL);

		perf_event_reset(fd[0]);
		perf_event_enable(fd[0]);

		loadflush((volatile void *) claddr);

		perf_event_disable(fd[0]);
		for (j = 0; j < nevents; j++)
			perf_event_read(fd[j], &evcount[j]);
	}

	for (j = 0; j < nevents; j++)
		printf("%32s : total = %-10ld \tmin = %-10ld \tmax = %-10ld \tavg = %-10ld\n",
		       evname[j],
		       evcount[j].total,
		       evcount[j].min, evcount[j].max,
		       evcount[j].total / clcount);

	munmap((void *) addr, npages * pgsize);
	free(evcount);
	free(fd);

	return EXIT_SUCCESS;
}
