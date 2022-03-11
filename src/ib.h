#ifndef IB_H
#define IB_H

#include <bits/stdint-uintn.h>
#include <infiniband/verbs.h>
#include <byteswap.h>
#include <arpa/inet.h>

#include "plasma_manager.h"
#include "uthash.h"
#include "utarray.h"

/* IB info for connection between two managers 
	 Should contain extra info for indexing */
typedef struct {
	struct ibv_qp *qp;
	struct ibv_mr *mr;
	uint8_t *ib_buf;
	int64_t bufsize;

	// key and handle to construct a hash table
	char *ip_addr_port; // <address>:<port>
	UT_hash_handle ibpair_hh;
} IB_pair_info;

/* IB info for one manager process. Reuse for multiple queue pairs */
typedef struct {
	struct ibv_context *ctx;
	struct ibv_pd *pd;
	struct ibv_cq *cq;
	struct ibv_device_arrt dev_attr;
	struct ibv_port_attr port_attr;

	// a list of pair info here.
	IB_pair_info *pairs;
} IB_state;

/* QP info. Should exchange upon connection establishment */
typedef struct {
	uint16_t lid;
	uint32_t qp_num;
}__attribute__((packed)) QP_info;

/* This function moves qp from state reset to rts */
int bringup_qp();

/* Bring up IB connections of a pair of managers */
int setup_ib_conn();
void free_ib_conn();

/* Prepare IB connections for one process, e.g. device query */
int setup_ib(IB_state *ib_state);
void free_ib(IB_state *ib_state);

/* IB Send/Recv for a chunk of transfered buffer */
int ib_send_object_chunk(client_connection *conn, plasma_request_buffer *buf);
int ib_recv_object_chunk(client_connection *conn, plasma_request_buffer *buf);

#endif // IB_H