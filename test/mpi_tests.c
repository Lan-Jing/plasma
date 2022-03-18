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
#include "../src/timer.h"

int size, rank;
int object_size = 4096,
	fetch_num = 100;
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

int plasma_local_benchmarks(plasma_connection *conn, int64_t object_size)
{
	if(rank)
		return 0;
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

		// Create again for remote fetch later
		plasma_create(conn, id, object_size, NULL, 0, &data);
		// memcpy(data, tmp, sizeof(uint8_t)*object_size);
		memcpy(data, tmp_str, strlen(tmp_str)+1);
		plasma_seal(conn, id);
		plasma_release(conn, id);
	}

	// Report latency for local store operations
	printf("Average latency for plasma_create: %lu ns\n", time_avg(timers[0], fetch_num));
	printf("Average latency for plasma_get   : %lu ns\n", time_avg(timers[1], fetch_num));
	printf("Average latency for plasma_delete: %lu ns\n", time_avg(timers[2], fetch_num));
	return 0;
}

int plasma_network_benchmarks(plasma_connection *conn, uint64_t object_size)
{
	if(!rank)
		return 0;
	struct timespec timer, start, end;
	memset(&timer, 0, sizeof(struct timespec));

	int *is_fetched = (int*)malloc(fetch_num * sizeof(int));
	memset(is_fetched, 0, fetch_num * sizeof(int));

	clock_gettime(CLOCK_REALTIME, &start);
	plasma_fetch(conn, fetch_num, ids, is_fetched);
	clock_gettime(CLOCK_REALTIME, &end);
	time_add(&timer, time_diff(start, end));

	for(int i = 0;i < fetch_num;i++)
		assert(is_fetched[i] != 0);

	// Report latency for batched fetch requests
	printf("Average latency for %d batched fetch requests: %lu ns\n", fetch_num, time_avg(timer, fetch_num));

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

	if(rank == 0) {
		generate_ids(fetch_num);
		
		LOG_DEBUG("Local benchmark on MPI process at rank %d", rank);
		int res = plasma_local_benchmarks(conn, (uint64_t)object_size);
		assert(res == 0);

		// should send object ids to the remote client. At this time no object is present on the other side.
		MPI_Send(ids, fetch_num * sizeof(object_id), MPI_BYTE, 1, 0, MPI_COMM_WORLD);
	} else {
		ids = (object_id*)malloc(sizeof(object_id) * fetch_num);
		assert(ids != NULL);

		MPI_Recv(ids, fetch_num * sizeof(object_id), MPI_BYTE, 0, 0, MPI_COMM_WORLD, NULL);
		
		// Test remote fetch, measure bandwidth and latency.
		LOG_DEBUG("Network benchmark on MPI process at rank %d", rank);
		int res = plasma_network_benchmarks(conn, (uint64_t)object_size);
		assert(res == 0);
	}
	MPI_Barrier(MPI_COMM_WORLD); // Keep master process alive while doing remote testing.

	destroy_ids();
	MPI_Finalize();
	return 0;
}