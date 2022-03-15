#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <assert.h>
#include <mpi.h>

#include "../src/plasma.h"
#include "../src/plasma_client.h"

int size, rank;
int object_size = 4096, 
	num_fetch = 10000;

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
			num_fetch = atoi(optarg);
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
	MPI_Barrier(MPI_COMM_WORLD);	

	// Perform local operations before we go across nodes.
	object_id id;
	memset(&id, 0, UNIQUE_ID_SIZE);
	uint8_t *data;
	plasma_create(conn, id, (int64_t)object_size, NULL, 0, &data);
	assert(data != NULL);
	
	char *test_str = "hello world";
	memcpy(data, test_str, strlen(test_str));
	plasma_seal(conn, id);

	plasma_get(conn, id, (int64_t*)&object_size, &data, NULL, NULL);
	assert(data != NULL);
	LOG_DEBUG("Local operations testsed, Object size: %d; data: %s", 
			  object_size, (char*)data);

	MPI_Finalize();
	return 0;
}