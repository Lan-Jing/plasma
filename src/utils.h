#include <time.h>

/* Helper functions to count wall time interval of two points */
struct timespec time_diff(struct timespec start, struct timespec end);
void time_add(struct timespec *des, struct timespec source);
uint64_t time_avg(struct timespec t, int num);

/* Helper functions to shuffle an object array for benchmark */
void shuffle(void *array, size_t n, size_t size);