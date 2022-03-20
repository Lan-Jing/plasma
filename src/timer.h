#include <time.h>

/* Helper functions to count wall time interval of two points */
struct timespec time_diff(struct timespec start, struct timespec end);
void time_add(struct timespec *des, struct timespec source);
uint64_t time_avg(struct timespec t, int num);