#include <stdint.h>
#include "timer.h"

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