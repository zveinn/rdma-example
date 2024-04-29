// #define LOCAL_HEADER (0)
#define SERVER_HEADER (1)
#ifdef LOCAL_HEADER
#include "rdma/rdma_cma.h"
#endif

#ifdef SERVER_HEADER
#include <rdma/rdma_cma.h>
#endif
#include <stdio.h>

void hello() { printf("Hello from C in another file"); }

struct rdma_event_channel *rdma_create_event_channel_wrapper() {
  return rdma_create_event_channel();
}
