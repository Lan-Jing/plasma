#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <assert.h>
#include <inttypes.h>
#include <time.h>
#include <mpi.h>

#include "../src/plasma.h"
#include "../src/plasma_client.h"
#include "../src/utils.h"

int size, rank;
int object_size = 4096 * 16,
	fetch_num   = 1000,
	warmup_num  = 10;
object_id *ids;

char *tmp_str = "hello world";

void generate_ids(int num_objects)
{
	assert(ids == NULL);
	LOG_DEBUG("Size of object_id %ld", sizeof(object_id));
	ids = (object_id*)malloc(sizeof(object_id) * num_objects);
	assert(ids != NULL);

	for(int i = 0;i < num_objects;i++)
		ids[i] = globally_unique_id();
}

void destroy_ids()
{
	assert(ids != NULL);
	free(ids);
}

/**
 * @brief Test plasma inter-node operations and record performace
 * 
 * @param conn plasma context returned by plasma_connect
 * @param object_size size of the object tested
 * @return int 
 */
int plasma_local_benchmarks(plasma_connection *conn, int64_t object_size)
{
	struct timespec timers[3], start, end;
	memset(timers, 0, sizeof(struct timespec)*3);

	uint8_t *data, *tmp;
	int64_t  size;
	tmp = (uint8_t*)malloc(sizeof(uint8_t)*object_size);
	assert(tmp != NULL);
	memset(tmp, 1, sizeof(uint8_t)*object_size);

	for(int i = 0;i < fetch_num;i++) {
		object_id id = ids[i];

		clock_gettime(CLOCK_REALTIME, &start);
		plasma_create(conn, id, object_size, NULL, 0, &data);
		memcpy(data, tmp, sizeof(uint8_t)*object_size);
		clock_gettime(CLOCK_REALTIME, &end);
		time_add(&timers[0], time_diff(start, end));

		assert(data != NULL);
		plasma_seal(conn, id);
		plasma_release(conn, id); // also call release after plasma_create
		data = NULL;

		clock_gettime(CLOCK_REALTIME, &start);
		plasma_get(conn, id, &size, &data, NULL, NULL);
		clock_gettime(CLOCK_REALTIME, &end);
		time_add(&timers[1], time_diff(start, end));

		assert(size != 0 && data != NULL);
		plasma_release(conn, id);

		clock_gettime(CLOCK_REALTIME, &start);
		plasma_delete(conn, id);
		clock_gettime(CLOCK_REALTIME, &end);
		time_add(&timers[2], time_diff(start, end));

		// Create these objects again for remote fetch later
		plasma_create(conn, id, object_size, NULL, 0, &data);
		// memcpy(data, tmp, sizeof(uint8_t)*object_size);
		memcpy(data, tmp_str, strlen(tmp_str)+1);
		plasma_seal(conn, id);
		plasma_release(conn, id);
	}

	// Report latency for local store operations
	if(rank == size-1) {
		printf("Average latency for plasma_create: %6lu ns\n", time_avg(timers[0], fetch_num));
		printf("Average latency for plasma_get   : %6lu ns\n", time_avg(timers[1], fetch_num));
		printf("Average latency for plasma_delete: %6lu ns\n", time_avg(timers[2], fetch_num));
	}
	return 0;
}

/**
 * @brief Test plasma remote operations, aka plasma_fetch and its performance
 * 
 * @param conn plasma context returned by plasma_connect
 * @param object_size size of the object tested
 * @return int 
 */
int plasma_network_benchmarks(plasma_connection *conn, uint64_t object_size)
{
	// we need this as we don't want to fetch local objects
	object_id *arr = ids + fetch_num; 
	// we fetch objects from all other manager processes
	fetch_num *= size-1; 
	// then shuffle ids to randomize remote fetch calls
	shuffle(arr, fetch_num, sizeof(object_id));

	struct timespec timer, start, end;
	memset(&timer, 0, sizeof(struct timespec));

	int *is_fetched = (int*)malloc(fetch_num * sizeof(int));
	memset(is_fetched, 0, fetch_num * sizeof(int));

	/* A warmup to hide performance degradation from connection setup */
	plasma_fetch(conn, warmup_num, arr, is_fetched);
	printf("Network benchmark starts\n");

	clock_gettime(CLOCK_REALTIME, &start);
	plasma_fetch(conn, fetch_num-warmup_num, arr+warmup_num, is_fetched+warmup_num);
	clock_gettime(CLOCK_REALTIME, &end);
	time_add(&timer, time_diff(start, end));

	for(int i = warmup_num;i < fetch_num;i++) {
		assert(is_fetched[i] != 0);
		if(i == warmup_num) {
			uint8_t *data = NULL;
			int64_t size = 0;
			plasma_get(conn, arr[i], &size, &data, NULL, NULL);
			LOG_DEBUG("Data fetched: %s", data);
		}
	}

	// Report latency for batched fetch requests
	printf("Average latency for %d batched fetch requests of object size %ld: %6lu ns\n", 
		   fetch_num, object_size, time_avg(timer, fetch_num));
	printf("Test done in: %lf(s)\nThroughput: %lf(ops)\n", 
		   time_avg(timer, 1)/1e9, (double)fetch_num*1e9/time_avg(timer, 1));

	free(is_fetched);
	return 0;
}

int main(int argc, char *argv[])
{
	MPI_Init(NULL, NULL);
	MPI_Comm_size(MPI_COMM_WORLD, &size);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	plasma_connection *conn = NULL;
	char *store_socket_name = NULL, 
		 *manager_addr = NULL;
	int manager_port = 0;

	// configuration of the benchmark	
	int c;
	while((c = getopt(argc, argv, "s:m:p:S:N:")) != -1) {
		switch(c) {
		case 's':
			store_socket_name = optarg;
			break;
		case 'm':
			manager_addr = optarg;
			break;
		case 'p':
			manager_port = atoi(optarg);
			break;
		case 'S':
			object_size = atoi(optarg);
			break;
		case 'N':
			fetch_num = atoi(optarg);
			break;
		default:
			LOG_ERR("Unknown option %c", c);
			exit(-1);
		}
	}

	// Connect to the local store and manager processes.
	if(!store_socket_name) {
		LOG_ERR("Missing socket name for Plasma Store, specify with -s");
		exit(-1);
	}
	if(!manager_addr || !manager_port) {
		LOG_ERR("Missing address of Plasma Manager, specify with -m(addr) and -p(port)");
		exit(-1);
	}
	conn = plasma_connect(store_socket_name, manager_addr, manager_port);
	assert(conn != NULL);
	MPI_Barrier(MPI_COMM_WORLD); // make sure both clients are connected.

	if(rank > 0) {
		generate_ids(fetch_num);

		LOG_DEBUG("Local benchmark on MPI process at rank %d", rank);
		int res = plasma_local_benchmarks(conn, (uint64_t)object_size);
		assert(res == 0);

		MPI_Gather(ids, fetch_num * sizeof(object_id), MPI_BYTE, 
				   NULL, 0, MPI_BYTE, 
				   0, MPI_COMM_WORLD);
		// MPI_Send(ids, fetch_num * sizeof(object_id), MPI_BYTE, 0, 0, MPI_COMM_WORLD);
		MPI_Barrier(MPI_COMM_WORLD);
	} else {
		ids = (object_id*)malloc(sizeof(object_id) * fetch_num * size);
		assert(ids != NULL);

		MPI_Gather(MPI_IN_PLACE, 0, MPI_BYTE,
				   ids, fetch_num * sizeof(object_id), MPI_BYTE,
				   0, MPI_COMM_WORLD);
		// MPI_Recv(ids, fetch_num * sizeof(object_id), MPI_BYTE, 1, 0, MPI_COMM_WORLD, NULL);
		MPI_Barrier(MPI_COMM_WORLD);

		// Test remote fetch, measure throughput and latency.
		LOG_DEBUG("Netowrk benchmark on MPI process at rank %d", rank);
		int res = plasma_network_benchmarks(conn, (uint64_t)object_size);
		assert(res == 0);
	}
	MPI_Barrier(MPI_COMM_WORLD); // Keep master process alive while doing remote testing.

	destroy_ids();
	MPI_Finalize();
	return 0;
}