#ifndef IB_H
#define IB_H

#include <infiniband/verbs.h>
#include <arpa/inet.h>
#include <byteswap.h>

#include "io.h"
#include "plasma_manager.h"
#include "uthash.h"

/**
 * @brief an as-portable-as-possible macro to do 64-bits "htonl" operation
 * 
 */
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
static inline uint64_t htonll (uint64_t x) { return bswap_64(x); }
static inline uint64_t ntohll (uint64_t x) { return bswap_64(x); }
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
static inline uint64_t htonll (uint64_t x) { return x; }
static inline uint64_t ntohll (uint64_t x) { return x; }
#else
#error __BYTE_ORDER__ is neither __ORDER_LITTLE_ENDIAN__ nor __ORDER_BIG_ENDIAN__
#endif

#define IB_MTU  IBV_MTU_4096
#define IB_PORT 1
#define IB_SL   0
#define CQE_NUM 100
#define IB_READ_MIN_SIZE 1024 * 1024

/* IB info for connection between any two managers 
   Should contain extra info for indexing */
typedef struct {
  struct ibv_qp *qp;
  struct ibv_wc *wc;
  struct ibv_cq *cq;
  struct ibv_mr *recv_mr;
  struct ibv_mr *send_mr;
  struct ibv_mr *read_mr;
  uint8_t *ib_recv_buf; // send/recv buffers are pre-pinned
  uint8_t *ib_read_buf; // read buffer is pinned and registered on-the-fly
  int64_t bufsize;

  uint32_t rkey;
  uint64_t raddr;

  // key and handle to construct a hash table
  int slid;
  UT_hash_handle hh;
} IB_pair_info;

/* IB info for one manager process. Reuse for multiple queue pairs */
typedef struct {
  struct ibv_context *ctx;
  struct ibv_pd *pd;
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
  uint32_t rkey;
  uint64_t raddr;
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
 * @brief This is simply an get_manager_connection function that does extra
          IB connection setup. 
 * 
 * @param state Our plasma manager state.
 * @param ip_addr The IP address of the remote manager we want to connect to.
 * @param port The port that the remote manager is listening on.
 * @return A pointer to the connection to the remote manager.
 */
client_connection *get_manager_ib_connection(plasma_manager_state *state,
                                             const char *ip_addr, int port);

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
 * @param buf a request buffer containing data need to be sent
 */
void ib_send_object_chunk(client_connection *conn, plasma_request_buffer *buf);
int  ib_recv_object_chunk(client_connection *conn, plasma_request_buffer *buf);

/**
 * @brief Send/Recv parameter for RDMA read from one side; 
 *        Also on-the-fly register buffer from both sides before transferring data
 * 
 * @param conn Client connection context to the other manager
 * @param buf a request buffer containing data need to be read
 */
void ib_send_read_info(client_connection *conn, plasma_request_buffer *buf);
void ib_recv_read_info(client_connection *conn, plasma_request_buffer *buf);

/**
 * @brief One-sided read for a tranferred buffer; the other side waits for an ACK
 * 
 * @param conn Client connection context to the other manager
 * @param buf a request buffer containing data need to be read
 */
void ib_wait_object_chunk(client_connection *conn, plasma_request_buffer *buf);
int  ib_read_object_chunk(client_connection *conn, plasma_request_buffer *buf);

#endif // IB_H