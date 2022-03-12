#ifndef IB_H
#define IB_H

#include <bits/stdint-uintn.h>
#include <infiniband/verbs.h>
#include <arpa/inet.h>

#include "io.h"
#include "plasma_manager.h"
#include "uthash.h"

#define IB_MTU  IBV_MTU_4096
#define IB_PORT 1
#define IB_SL   0

/* IB info for connection between any two managers 
   Should contain extra info for indexing */
typedef struct {
  struct ibv_qp *qp;
  struct ibv_mr *mr;
  uint8_t *ib_buf;
  int64_t bufsize;

  // key and handle to construct a hash table
  int sock_fd;
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
  // potentially more for Read/Write
}__attribute__((packed)) QP_info;

/* This function moves qp from state reset to rts */
int bringup_qp(struct ibv_qp *qp, QP_info qp_info);

/* Bring up IB connections of a pair of managers */
int sock_send_qp_info(int fd, QP_info  *local_qp_info);
int sock_recv_qp_info(int fd, QP_info *remote_qp_info);
int setup_ib_conn(IB_state *ib_state, enum manager_state mstate, 
                  int fd);
void free_ib_conn(IB_state *ib_state, int fd);

/* Prepare IB connections for one process, e.g. device query */
int setup_ib(IB_state *ib_state);
void free_ib(IB_state *ib_state);

/* IB Send/Recv for a chunk of transfered buffer */
int ib_send_object_chunk(client_connection *conn, plasma_request_buffer *buf);
int ib_recv_object_chunk(client_connection *conn, plasma_request_buffer *buf);

#endif // IB_H