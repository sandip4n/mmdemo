#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#include "maps.h"

int main(void)
{
	size_t pgsize;
	void *mem[2];

	printf("\n** Initial **\n");
	show_maps();

	pgsize = getpagesize();
	mem[0] = mmap((void *) 0x7fff00000000UL, pgsize,
		      PROT_READ | PROT_WRITE,
		      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	assert(mem[0] != MAP_FAILED);

	printf("\n** After first mmap() **\n");
	show_maps();

	mem[1] = mmap(mem[0] + pgsize, pgsize,
		      PROT_READ | PROT_WRITE,
		      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	assert(mem[1] != MAP_FAILED);

	printf("\n** After second mmap() **\n");
	show_maps();

	mprotect(mem[1], pgsize, PROT_READ | PROT_WRITE | PROT_EXEC);
	printf("\n** After first mprotect() **\n");
	show_maps();

	mprotect(mem[1], pgsize, PROT_READ | PROT_WRITE);
	printf("\n** After second mprotect() **\n");
	show_maps();

	munmap(mem[0], 2 * pgsize);

	return EXIT_SUCCESS;
}
