#ifdef IB

#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>

#include "uthash.h"
#include "common.h"
#include "io.h"
#include "ib.h"

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
  IB_pair_info *pair = (IB_pair_info*)malloc(sizeof(IB_pair_info));
  pair->sock_fd = fd;
  pair->bufsize = BUFSIZE;
  pair->ib_buf  = (uint8_t*)malloc(BUFSIZE);
  CHECKM(pair->ib_buf != NULL, "Failed to allocate IB buffer.");

  pair->mr = ibv_reg_mr(ib_state->pd, (void*)pair->ib_buf, pair->bufsize,
                        IBV_ACCESS_LOCAL_WRITE |
                        IBV_ACCESS_REMOTE_READ |
                        IBV_ACCESS_REMOTE_WRITE);
  CHECKM(pair->mr != NULL, "Failed to register Memory Region.");

  struct ibv_qp_init_attr qp_init_attr = {
    .send_cq = ib_state->cq,
    .recv_cq = ib_state->cq,
    .cap = {
      .max_send_wr = ib_state->dev_attr.max_qp_wr*3/4,
      .max_recv_wr = ib_state->dev_attr.max_qp_wr*3/4,
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
  bringup_qp(pair->qp, remote_qp_info);

  HASH_ADD_INT(ib_state->pairs, sock_fd, pair);
  LOG_DEBUG("Establish IB Connection to <%d:%d>", remote_qp_info.lid, remote_qp_info.qp_num);
  return 0;
}

void free_ib_conn(IB_state *ib_state, int fd)
{
  IB_pair_info *pair;
  HASH_FIND_INT(ib_state->pairs, &fd, pair);
  if(pair == NULL) {
    LOG_DEBUG("IB Connection to fd:%d not found.", fd);
    return ;
  }
  HASH_DEL(ib_state->pairs, pair);

  if(pair->qp != NULL)
    ibv_destroy_qp(pair->qp);
  if(pair->mr != NULL)
    ibv_dereg_mr(pair->mr);
  if(pair->ib_buf != NULL)
    free(pair->ib_buf);
  free(pair);
}

int setup_ib(IB_state *ib_state)
{
  CHECKM(ib_state != NULL, "NULL IB state passed in.");
  int res = 0;
  struct ibv_device **dev_list = NULL;

  dev_list = ibv_get_device_list(NULL);
  CHECKM(dev_list != NULL, "Failed to fetch ib device list.");

  ib_state = (IB_state*)malloc(sizeof(IB_state));
  memset(ib_state, 0, sizeof(IB_state));
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

#endif // IB