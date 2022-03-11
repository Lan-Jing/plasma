#include "ib.h"
#include "common.h"

int setup_ib(IB_state *ib_state)
{
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