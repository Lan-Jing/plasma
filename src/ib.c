#ifdef IB

#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "plasma_client.h"
#include "io.h"
#include "ib.h"
#include "state/db.h"
#include "state/object_table.h"

/* Below are duplicate definitions of some core structures
   defined in plasma_manager.c, this allow me to program IB-relevant functions
   inside this source file.
*/
typedef struct plasma_manager_state {
  /** Our address. */
  uint8_t addr[4];
  /** Our port. */
  int port;
  /** Event loop. */
  event_loop *loop;
  db_handle *db;
  /** Connection to the local plasma store for reading or writing data. */
  plasma_connection *plasma_conn;
  /** Hash table of all contexts for active connections to
   *  other plasma managers. These are used for writing data to
   *  other plasma stores. */
  client_connection *manager_connections;
  /** Hash table of outstanding fetch requests. The key is
   *  object id, value is a list of connections to the clients
   *  who are blocking on a fetch of this object. */
  client_object_connection *fetch_connections;
#ifdef IB
  /* Struct holding IB contexts for this manager. */
  IB_state *ib_state;
#endif
} plasma_manager_state;

/* Context for a client connection to another plasma manager. */
typedef struct client_connection {
  /** Current state for this plasma manager. This is shared
   *  between all client connections to the plasma manager. */
  plasma_manager_state *manager_state;
  /** Current position in the buffer. */
  int64_t cursor;
  /** Buffer that this connection is reading from. If this is a connection to
   *  write data to another plasma store, then it is a linked
   *  list of buffers to write. */
  /* TODO(swang): Split into two queues, data transfers and data requests. */
  plasma_request_buffer *transfer_queue;
  /** File descriptor for the socket connected to the other
   *  plasma manager. */
  int fd;
#ifdef IB
  int slid;
#endif
  /** The objects that we are waiting for and their callback
   *  contexts, for either a fetch or a wait operation. */
  client_object_connection *active_objects;
  /** The number of objects that we have left to return for
   *  this fetch or wait operation. */
  int num_return_objects;
  /** Fields specific to connections to plasma managers.  Key that uniquely
   * identifies the plasma manager that we're connected to. We will use the
   * string <address>:<port> as an identifier. */
  char *ip_addr_port;
  /** Handle for the uthash table. */
  UT_hash_handle manager_hh;
} client_connection;

int sock_send_qp_info(int fd, QP_info *local_qp_info)
{
  QP_info qp_info_buf = {
    .qp_num = htonl(local_qp_info->qp_num), 
    .lid    = htons(local_qp_info->lid),
  };

  int res = write_bytes(fd, (uint8_t*)&qp_info_buf, sizeof(QP_info));
  CHECKM(res == 0, "Failed to send out local QP info.");

  return 0;
}

int sock_recv_qp_info(int fd, QP_info *remote_qp_info)
{
  QP_info qp_info_buf;

  int res = read_bytes(fd, (uint8_t*)&qp_info_buf, sizeof(QP_info));
  CHECKM(res == 0, "Failed to get remote QP info.");

  remote_qp_info->qp_num = ntohl(qp_info_buf.qp_num);
  remote_qp_info->lid    = ntohs(qp_info_buf.lid);
  return 0;
}

/* Below are wrappers of ibv_post_send/ibv_post_recv operations */
int post_send(unsigned char *buf, uint32_t req_size, uint32_t lkey, 
              uint64_t wr_id, struct ibv_qp *qp)
{
  struct ibv_send_wr *bad_send_wr;

  struct ibv_sge list = {
    .addr   = (uintptr_t)buf,
    .length = req_size,
    .lkey   = lkey
  };

  struct ibv_send_wr send_wr = {
    .wr_id      = wr_id,
    .sg_list    = &list,
    .num_sge    = 1,
    .opcode     = IBV_WR_SEND,
    .send_flags = IBV_SEND_SIGNALED,
  };

  return ibv_post_send(qp, &send_wr, &bad_send_wr);
}

int post_recv(unsigned char *buf, uint32_t req_size, uint32_t lkey, 
              uint64_t wr_id, struct ibv_qp *qp)
{
  struct ibv_recv_wr *bad_recv_wr;

  struct ibv_sge list = {
    .addr   = (uintptr_t)buf,
    .length = req_size,
    .lkey   = lkey
  };

  struct ibv_recv_wr recv_wr = {
    .wr_id   = wr_id,
    .sg_list = &list,
    .num_sge = 1
  };

  return ibv_post_recv(qp, &recv_wr, &bad_recv_wr);
}


int bringup_qp(struct ibv_qp *qp, QP_info remote_qp_info)
{
  int res = 0;
  {
  struct ibv_qp_attr qp_attr = {
    .qp_state        = IBV_QPS_INIT,
    .pkey_index      = 0,
    .port_num        = IB_PORT,
    .qp_access_flags = IBV_ACCESS_LOCAL_WRITE |
                       IBV_ACCESS_REMOTE_READ |
                       IBV_ACCESS_REMOTE_WRITE |
                       IBV_ACCESS_REMOTE_ATOMIC,
  };
  res = ibv_modify_qp(qp, &qp_attr,
                      IBV_QP_STATE | IBV_QP_PKEY_INDEX |
                      IBV_QP_PORT  | IBV_QP_ACCESS_FLAGS);
  CHECKM(res == 0, "Failed to modify QP to state INIT.");
  }
  {
  struct ibv_qp_attr qp_attr = {
    .qp_state              = IBV_QPS_RTR,
    .path_mtu              = IB_MTU,
    .dest_qp_num           = remote_qp_info.qp_num,
    .rq_psn                = 0,
    .max_dest_rd_atomic    = 1,
    .min_rnr_timer         = 12,
    .ah_attr.is_global     = 0,
    .ah_attr.dlid          = remote_qp_info.lid,
    .ah_attr.sl            = IB_SL,
    .ah_attr.port_num      = IB_PORT,
    .ah_attr.src_path_bits = 0,
  };
  
  res = ibv_modify_qp(qp, &qp_attr,
                      IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
                      IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER);
  CHECKM(res == 0, "Failed to modify QP to state RTR.");
  }
  {
  struct ibv_qp_attr qp_attr = {
    .qp_state      = IBV_QPS_RTS,
    .timeout       = 14,
    .retry_cnt     = 7,
    .rnr_retry     = 7,
    .sq_psn        = 0,
    .max_rd_atomic = 1,
  };
  res = ibv_modify_qp(qp, &qp_attr,
                      IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | 
                      IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC);
  CHECKM(res == 0, "Failed to modify QP to state RTS.");
  }
  return 0;
}

int setup_ib_conn(IB_state *ib_state, int fd, enum manager_state mstate)
{
  CHECKM(ib_state != NULL, "Set up IB state before connecting queue pairs.");
  IB_pair_info *pair = (IB_pair_info*)malloc(sizeof(IB_pair_info));
  CHECKM(pair != NULL, "Failed to allocate queue pair info");

  struct ibv_qp_init_attr qp_init_attr = {
    .send_cq = ib_state->cq,
    .recv_cq = ib_state->cq,
    .cap = {
      .max_send_wr = ib_state->dev_attr.max_qp_wr/4,
      .max_recv_wr = ib_state->dev_attr.max_qp_wr/4,
      .max_send_sge = 1,
      .max_recv_sge = 1,
    },
    .qp_type = IBV_QPT_RC,
  };
  pair->qp = ibv_create_qp(ib_state->pd, &qp_init_attr);
  CHECKM(pair->qp != NULL, "Failed to create qp.");

  QP_info remote_qp_info, local_qp_info = {
    .lid = ib_state->port_attr.lid,
    .qp_num = pair->qp->qp_num,
  };
  if(mstate == MANAGER_CLIENT) {
    sock_send_qp_info(fd, &local_qp_info);
    sock_recv_qp_info(fd, &remote_qp_info);
  } else {
    sock_recv_qp_info(fd, &remote_qp_info);
    sock_send_qp_info(fd, &local_qp_info);
  }

  IB_pair_info *tmp;
  int tmp_slid = (int)remote_qp_info.lid; // HASH_FIND fails if type doesn't match
  HASH_FIND_INT(ib_state->pairs, &tmp_slid, tmp);
  if(tmp != NULL) {
    LOG_DEBUG("IB connection to lid %d already exists", remote_qp_info.lid);
    ibv_destroy_qp(pair->qp);
    free(pair);
    return tmp_slid;
  } else {
    // Now we can establish a unique connection
    pair->slid = tmp_slid;
  }

  pair->wc = (struct ibv_wc*)malloc(sizeof(struct ibv_wc) * CQE_NUM);
  CHECKM(pair->wc != NULL, "Failed to allocate completion buffer of size %d.", CQE_NUM);
  
  pair->bufsize = BUFSIZE;
  pair->ib_buf  = (uint8_t*)malloc(BUFSIZE);
  CHECKM(pair->ib_buf != NULL, "Failed to allocate IB buffer of size %d.", BUFSIZE);

  pair->mr = ibv_reg_mr(ib_state->pd, (void*)pair->ib_buf, pair->bufsize,
                        IBV_ACCESS_LOCAL_WRITE |
                        IBV_ACCESS_REMOTE_READ |
                        IBV_ACCESS_REMOTE_WRITE);
  CHECKM(pair->mr != NULL, "Failed to register Memory Region.");

  bringup_qp(pair->qp, remote_qp_info);
  /* Turns out that we must PRE-post receive work requests before taking in send requests */
  int res = post_recv(pair->ib_buf, pair->bufsize, pair->mr->lkey,
                      (uint64_t)pair->ib_buf, pair->qp);
  CHECKM(res == 0, "Failure detected at ibv_post_recv");

  HASH_ADD_INT(ib_state->pairs, slid, pair);
  LOG_DEBUG("IB port %d connects to <%d:%d>", 
            local_qp_info.lid, pair->slid, remote_qp_info.qp_num);
  return pair->slid;
}

void free_ib_conn(IB_state *ib_state, int slid)
{
  IB_pair_info *pair = NULL;
  HASH_FIND_INT(ib_state->pairs, &slid, pair);
  if(pair == NULL) {
    LOG_DEBUG("IB Connection to lid:%d not found.", slid);
    return ;
  }
  HASH_DEL(ib_state->pairs, pair);

  if(pair->qp != NULL)
    ibv_destroy_qp(pair->qp);
  if(pair->mr != NULL)
    ibv_dereg_mr(pair->mr);
  if(pair->ib_buf != NULL)
    free(pair->ib_buf);
  if(pair->wc != NULL)
    free(pair->wc);
  free(pair);
}

int setup_ib(IB_state *ib_state)
{
  CHECKM(ib_state != NULL, "Malloc IB state before passing it in.");
  memset(ib_state, 0, sizeof(IB_state));

  int res = 0;
  struct ibv_device **dev_list = NULL;

  dev_list = ibv_get_device_list(NULL);
  CHECKM(dev_list != NULL, "Failed to fetch ib device list.");

  ib_state->ctx = ibv_open_device(*dev_list); // open the first device;
  CHECKM(ib_state->ctx != NULL, "Failed to open ib device.");

  ib_state->pd = ibv_alloc_pd(ib_state->ctx);
  CHECKM(ib_state->pd != NULL, "Failed to allocate Protection Domain.");

  res = ibv_query_device(ib_state->ctx, &(ib_state->dev_attr));
  CHECKM(res == 0, "Failed to query IB device info.");

  res = ibv_query_port(ib_state->ctx, IB_PORT, &(ib_state->port_attr));
  CHECKM(res == 0, "Failed to query IB port info.");

  /* So far we will let queue pairs share one cq, 
     since polling cq has constant performance */
  ib_state->cq = ibv_create_cq(ib_state->ctx, ib_state->dev_attr.max_cqe,
                               NULL, NULL, 0);
  CHECKM(ib_state->cq != NULL, "Failed to create cq.");

  ibv_free_device_list(dev_list);
  return 0;
}

// should ensure that qp_info is empty before calling this.
void free_ib(IB_state *ib_state)
{
  if(ib_state == NULL)
    return ;

  if(ib_state->cq != NULL) 
    ibv_destroy_cq(ib_state->cq);

  if(ib_state->pd != NULL)
    ibv_dealloc_pd(ib_state->pd);

  if(ib_state->ctx != NULL)
    ibv_close_device(ib_state->ctx);

  free(ib_state);
}

void ib_send_object_chunk(client_connection *conn, plasma_request_buffer *buf)
{
  CHECKM(buf != NULL, "NULL buffer passed in");
  // Using lid to hash find the IB connection state.
  IB_pair_info *pair = NULL;
  HASH_FIND_INT(conn->manager_state->ib_state->pairs, &conn->slid, pair);
  CHECKM(pair != NULL, "Manager connected at lid %d not found", conn->slid);

  uint32_t req_size = buf->data_size + buf->metadata_size - conn->cursor;
  if(req_size == 0) {
    LOG_DEBUG("Writing to manager %d finished", conn->slid);
    conn->cursor = 0;
    plasma_release(conn->manager_state->plasma_conn, buf->object_id);
    return ;
  }

  LOG_DEBUG("cursor at %ld, data sent: %s", conn->cursor, buf->data + conn->cursor);
  LOG_DEBUG("Writing data through IB Send to manager at lid %d", conn->slid);
  if(req_size > BUFSIZE)
    req_size = BUFSIZE;
  
  memcpy(pair->ib_buf, buf->data + conn->cursor, req_size);
  int res = post_send(pair->ib_buf, req_size, pair->mr->lkey, 
                      (uint64_t)pair->ib_buf, pair->qp);
  CHECKM(res == 0, "Failure detected at ibv_post_send");
  conn->cursor += req_size;

  /* Check completion status after pushing a send */
  int num_cqe = ibv_poll_cq(conn->manager_state->ib_state->cq, 
                            CQE_NUM, pair->wc);
  LOG_DEBUG("Sender polling CQ, got %d CQEs", num_cqe);
  CHECKM(num_cqe >= 0, "Failed to poll CQ");
  for(int i = 0;i < num_cqe;i++) {
    LOG_DEBUG("Work request %" PRIu64 " status: %s", 
              pair->wc[i].wr_id, ibv_wc_status_str(pair->wc[i].status));
    CHECKM(pair->wc[i].status == IBV_WC_SUCCESS, 
           "Send failed with: %s", ibv_wc_status_str(pair->wc[i].status));
  }
}

int ib_recv_object_chunk(client_connection *conn, plasma_request_buffer *buf)
{
  CHECKM(buf != NULL, "NULL buffer passed in");
  IB_pair_info *pair = NULL;
  HASH_FIND_INT(conn->manager_state->ib_state->pairs, &conn->slid, pair);
  CHECKM(pair != NULL, "Manager connected at lid %d not found", conn->slid);

  uint32_t req_size = buf->data_size + buf->metadata_size - conn->cursor;
  if(req_size == 0) {
    LOG_DEBUG("Reading from manager %d finished", conn->slid);
    conn->cursor = 0;
    return 1; // done
  }
 
  /* On receiver side however, we first check the result of previous recv requests,
     to make sure we can poll the data buffer.
     Then generate a new recv request. */
  int num_cqe = ibv_poll_cq(conn->manager_state->ib_state->cq,
                            CQE_NUM, pair->wc);
  LOG_DEBUG("Receiver polling CQ, got %d CQEs", num_cqe);
  CHECKM(num_cqe >= 0, "Failed to poll CQ");
  for(int i = 0;i < num_cqe;i++) {
    LOG_DEBUG("Work request %" PRIu64 " status: %s", 
              pair->wc[i].wr_id, ibv_wc_status_str(pair->wc[i].status));
    CHECKM(pair->wc[i].status == IBV_WC_SUCCESS,
           "Recv failed with: %s", ibv_wc_status_str(pair->wc[i].status));
  }

  LOG_DEBUG("Reading data through IB Recv from manager at lid %d to %p",
            conn->slid, buf->data + conn->cursor);
  if(req_size > BUFSIZE)
    req_size = BUFSIZE;

  memcpy(buf->data + conn->cursor, pair->ib_buf, req_size);
  conn->cursor += req_size;
  LOG_DEBUG("data received: %s", buf->data);

  int res = post_recv(pair->ib_buf, pair->bufsize, pair->mr->lkey,
                      (uint64_t)pair->ib_buf, pair->qp);
  CHECKM(res == 0, "Failure detected at ibv_post_recv");
  return 0;
}

#endif // IB