#ifndef IB_H
#define IB_H

#include <infiniband/verbs.h>
#include <arpa/inet.h>

#include "io.h"
#include "plasma_manager.h"
#include "uthash.h"

#define IB_MTU  IBV_MTU_4096
#define IB_PORT 1
#define IB_SL   0
#define CQE_NUM 100

/* IB info for connection between any two managers 
   Should contain extra info for indexing */
typedef struct {
  struct ibv_qp *qp;
  struct ibv_wc *wc;
  struct ibv_mr *recv_mr;
  struct ibv_mr *send_mr;
  uint8_t *ib_recv_buf;
  uint8_t *ib_send_buf;
  int64_t bufsize;

  // key and handle to construct a hash table
  int slid;
  UT_hash_handle hh;
} IB_pair_info;

/* IB info for one manager process. Reuse for multiple queue pairs */
typedef struct {
  struct ibv_context *ctx;
  struct ibv_pd *pd;
  struct ibv_cq *cq;
  struct ibv_device_attr dev_attr;
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
/**
 * @brief This function moves qp from state RESET to RTR then RTS
 * 
 * @param qp The quere pair we want to bring up
 * @param remote_qp_info Necessary infomation of the remote endpoint
 * @return int 
 */
int bringup_qp(struct ibv_qp *qp, QP_info remote_qp_info);

/**
 * @brief Send/Recv parameters of the local queue pair 
 * 
 * @param fd Socket fd that connects to the remote endpoint
 * @param local_qp_info Parameter of local qp
 * @return int 
 */
int sock_send_qp_info(int fd, QP_info  *local_qp_info);
int sock_recv_qp_info(int fd, QP_info *remote_qp_info);

/**
 * @brief Set up IB connection and do all necessary things for further message transfer
 * 
 * @param ib_state IB Context of a manager process
 * @param fd Socket fd that connects to the remote endpoint
 * @param mstate Whether this manager process connects as server(or client)
 * @return int 
 */
int setup_ib_conn(IB_state *ib_state, int fd, enum manager_state mstate);
void free_ib_conn(IB_state *ib_state, int fd);

/**
 * @brief Prepare IB connections for one process, e.g. device query
 * 
 * @param ib_state IB Context of a manager process 
 * @return int 
 */
int setup_ib(IB_state *ib_state);
void free_ib(IB_state *ib_state);

/**
 * @brief IB Send/Recv for a transferred buffer
 * 
 * @param conn Client connection context to the other manager
 * @param buf a request buffer containing data need to be 
 */
void ib_send_object_chunk(client_connection *conn, plasma_request_buffer *buf);
int  ib_recv_object_chunk(client_connection *conn, plasma_request_buffer *buf);

#endif // IB_H