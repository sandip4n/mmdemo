#ifndef __MAPS_H__
#define __MAPS_H__

#include <assert.h>
#include <stdio.h>

void show_maps(void)
{
	FILE *f;

	f = fopen("/proc/self/maps", "r");
	assert(f);

	while (!feof(f))
		putchar(fgetc(f));

	fclose(f);
}

#endif	/* __MAPS_H__ */
