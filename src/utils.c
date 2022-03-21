#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "utils.h"

struct timespec time_diff(struct timespec start, struct timespec end)
{
	struct timespec res;
	if(end.tv_nsec-start.tv_nsec < 0) {
		res.tv_nsec = end.tv_nsec-start.tv_nsec+1e9;
		res.tv_sec  = end.tv_sec-start.tv_sec-1;
	} else {
		res.tv_nsec = end.tv_nsec-start.tv_nsec;
		res.tv_sec  = end.tv_sec-start.tv_sec;
	}
	return res;
}

void time_add(struct timespec *des, struct timespec source)
{
	if(des->tv_nsec+source.tv_nsec >= 1e9) {
		des->tv_nsec += source.tv_nsec-1e9;
		des->tv_sec  += source.tv_sec+1;
	} else {
		des->tv_nsec += source.tv_nsec;
		des->tv_sec  += source.tv_sec;
	}
}

uint64_t time_avg(struct timespec t, int num)
{
	uint64_t res = (uint64_t)t.tv_nsec + (uint64_t)t.tv_sec * 1e9;

	return res/(uint64_t)num;
}

void shuffle(void *array, size_t n, size_t size)
{
	char tmp[size];
	char *arr = array;
	size_t stride = size * sizeof(char);

	if(n <= 1)
		return;
	size_t i;
	for(i = 0;i < n-1;i++) {
		size_t rnd = (size_t)rand();
		size_t j = i + rnd / (RAND_MAX / (n-i) + 1);

		memcpy(tmp, arr+j*stride, size);
		memcpy(arr+j*stride, arr+i*stride, size);
		memcpy(arr+i*stride, tmp, size);
	}
}
