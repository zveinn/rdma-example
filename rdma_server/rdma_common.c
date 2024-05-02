/*
 * Implementation of the common RDMA functions.
 *
 * Authors: Animesh Trivedi
 *          atrivedi@apache.org
 */

#include "rdma_common.h"
#include <stdint.h>

void show_rdma_cmid(struct rdma_cm_id *id) {
  if (!id) {
    debug("Passed ptr is NULL\n");
    return;
  }
  printf("RDMA cm id at %p \n", id);
  if (id->verbs && id->verbs->device)
    printf("dev_ctx: %p (device name: %s) \n", id->verbs,
           id->verbs->device->name);
  if (id->channel)
    printf("cm event channel %p\n", id->channel);
  printf("QP: %p, port_space %x, port_num %u \n", id->qp, id->ps, id->port_num);
}

void show_rdma_buffer_attr(struct rdma_buffer_attr *attr) {
  if (!attr) {
    debug("Passed attr is NULL\n");
    return;
  }
  printf("---------------------------------------------------------\n");
  printf("buffer attr, addr: %p , len: %u , stag : 0x%x \n",
         (void *)attr->address, (unsigned int)attr->length,
         attr->stag.local_stag);
  printf("---------------------------------------------------------\n");
}

struct ibv_mr *rdma_buffer_alloc(struct ibv_pd *pd, uint32_t size,
                                 enum ibv_access_flags permission) {
  struct ibv_mr *mr = NULL;
  if (!pd) {
    debug("Protection domain is NULL \n");
    return NULL;
  }
  void *buf = calloc(1, size);
  if (!buf) {
    debug("failed to allocate buffer, -ENOMEM\n");
    return NULL;
  }
  debug("Buffer allocated: %p , len: %u \n", buf, size);
  mr = rdma_buffer_register(pd, buf, size, permission);
  if (!mr) {
    free(buf);
  }
  return mr;
}

struct ibv_mr *rdma_buffer_register(struct ibv_pd *pd, void *addr,
                                    uint32_t length,
                                    enum ibv_access_flags permission) {
  struct ibv_mr *mr = NULL;
  if (!pd) {
    debug("Protection domain is NULL, ignoring \n");
    return NULL;
  }
  mr = ibv_reg_mr(pd, addr, length, permission);
  if (!mr) {
    debug("Failed to create mr on buffer, errno: %d \n", -errno);
    return NULL;
  }
  debug("Registered: %p , len: %u , stag: 0x%x \n", mr->addr,
        (unsigned int)mr->length, mr->lkey);
  return mr;
}

void rdma_buffer_free(struct ibv_mr *mr) {
  if (!mr) {
    debug("Passed memory region is NULL, ignoring\n");
    return;
  }
  void *to_free = mr->addr;
  rdma_buffer_deregister(mr);
  debug("Buffer %p free'ed\n", to_free);
  free(to_free);
}

void rdma_buffer_deregister(struct ibv_mr *mr) {
  if (!mr) {
    debug("Passed memory region is NULL, ignoring\n");
    return;
  }
  debug("Deregistered: %p , len: %u , stag : 0x%x \n", mr->addr,
        (unsigned int)mr->length, mr->lkey);
  ibv_dereg_mr(mr);
}

int get_rdma_cm_event(struct rdma_event_channel *echannel,
                      struct rdma_cm_event **cm_event) {
  int ret = 1;
  ret = rdma_get_cm_event(echannel, cm_event);
  if (ret) {
    debug("++EVENT(no event), errno: %d \n", -errno);
    return -errno;
  }

  // if (0 != (*cm_event)->status) {
  //   debug("++EVENT(invalid status): %d\n", (*cm_event)->status);
  //   ret = -((*cm_event)->status);
  //   rdma_ack_cm_event(*cm_event);
  //   return ret;
  // }

  debug("-------------------------\n");
  debug("++EVENT(%s) \n", rdma_event_str((*cm_event)->event));
  debug("++EVENT(STATUS) %d \n", (*cm_event)->status);
  debug("++EVENT(ID) %p \n", (*cm_event)->id);
  debug("++EVENT(PORT) %d \n", (*cm_event)->id->port_num);
  debug("++EVENT(QP) %d \n", (*cm_event)->param.conn.qp_num);
  debug("-------------------------\n");
  return ret;
}

uint32_t pollEventChannel(
    struct rdma_event_channel *channel,
    enum rdma_cm_event_type type,
    int expectedStatus,
    int timeout,
    struct rdma_cm_event **event) {

  int8_t ret = 0;
  struct timespec start_time;
  struct timespec loop_time;
  clock_gettime(CLOCK_REALTIME, &start_time);

  // struct pollfd pfd = {.fd = channel->fd, .events = POLLIN};

  while (1) {
    clock_gettime(CLOCK_REALTIME, &loop_time);
    uint64_t diff = timestampDiff(&loop_time, &start_time);
    if (diff > timeout) {
      return 0;
    }

    ret = rdma_get_cm_event(channel, event);
    if (ret) {
      continue;
    } else {
      break;
    }
  }

  if ((*event)->status != expectedStatus) {
    ret = -((*event)->status);
    int8_t ackRet = rdma_ack_cm_event(*event);
    return makeError(ret, ErrUnexpectedEventStatus, ackRet, 0);
  }

  if ((*event)->event != type) {
    int8_t ackRet = rdma_ack_cm_event(*event);
    return makeError(ret, ErrUnexpectedEventType, ackRet, 0);
  }

  int8_t ackRet = rdma_ack_cm_event(*event);
  if (ackRet) {
    return makeError(ret, ErrUnableToAckEvent, ackRet, 0);
  }
  //
  debug("++EVENT(%s) \n", rdma_event_str((*event)->event));
  // debug("++EVENT(ID) %p \n", (*cm_event)->id->event->id);
  debug("++EVENT(ID) %p \n", (*event)->id);
  debug("++EVENT(connQPN) %d \n", (*event)->param.conn.qp_num);
  debug("++EVENT(udQPN) %d \n", (*event)->param.ud.qp_num);
  if ((*event)->id) {
    debug("++EVENT(PortNum) %d \n", (*event)->id->port_num);
  }

  return ret;

  return 0;
}

int process_rdma_cm_event(struct rdma_event_channel *echannel, enum rdma_cm_event_type expected_event, struct rdma_cm_event **cm_event) {
  int ret = 1;
  ret = rdma_get_cm_event(echannel, cm_event);
  if (ret) {
    debug("++EVENT(no event), errno: %d \n", -errno);
    return ret;
  }
  debug("++EVENT(%s) \n", rdma_event_str((*cm_event)->event));

  if (0 != (*cm_event)->status) {
    debug("++EVENT(invalid status): %d\n", (*cm_event)->status);
    ret = -((*cm_event)->status);
    rdma_ack_cm_event(*cm_event);
    return ret;
  }

  if ((*cm_event)->event != expected_event) {
    debug("++EVENT(invalid type): %s [ expecting: %s ]",
          rdma_event_str((*cm_event)->event), rdma_event_str(expected_event));
    rdma_ack_cm_event(*cm_event);
    return -1;
  }

  // debug("++EVENT(ID) %p \n", (*cm_event)->id->event->id);
  debug("++EVENT(ID) %p \n", (*cm_event)->id);
  debug("++EVENT(ID) %d \n", (*cm_event)->id->port_num);
  debug("++EVENT(ID) %d \n", (*cm_event)->param.conn.qp_num);
  debug("++EVENT(ID) %d \n", (*cm_event)->param.ud.qp_num);
  return ret;
}

int process_work_completion_events(struct ibv_comp_channel *comp_channel,
                                   struct ibv_wc *wc) {
  struct ibv_cq *cq_ptr = NULL;
  void *context = NULL;
  int ret = -1;
  ret = ibv_get_cq_event(comp_channel, &cq_ptr, &context);
  if (ret) {
    debug("Failed to get next CQ event due to %d \n", -errno);
    return ret;
  }

  ret = ibv_req_notify_cq(cq_ptr, 0);
  if (ret) {
    debug("Failed to request further notifications %d \n", -errno);
    return ret;
  }

  ret = ibv_poll_cq(cq_ptr, 1, wc + 0);
  if (ret < 0) {
    debug("Failed to poll cq for wc due to %d \n", ret);
    return ret;
  }

  if (wc) {
    debug("WC ID: %lu \n", wc[0].wr_id);
    debug("WC SLID: %d \n", wc[0].slid);
    debug("WC QPnum: %d \n", wc[0].src_qp);
    debug("WC QPsrc: %d \n", wc[0].qp_num);
    debug("WC STATUS: %s \n", ibv_wc_status_str(wc[0].status));
    debug("WC OP: %d \n", wc[0].opcode);
  }

  ibv_ack_cq_events(cq_ptr, 1);
  return ret;
}

/* Code acknowledgment: rping.c from librdmacm/examples */
int get_addr(char *dst, struct sockaddr *addr) {
  struct addrinfo *res;
  int ret = -1;
  ret = getaddrinfo(dst, NULL, NULL, &res);
  if (ret) {
    debug("getaddrinfo failed - invalid hostname or IP address\n");
    return ret;
  }
  memcpy(addr, res->ai_addr, sizeof(struct sockaddr_in));
  freeaddrinfo(res);
  return ret;
}

uint32_t makeError(int8_t funcCode, int8_t minioCode, int8_t extra1, int8_t extra2) {
  uint8_t uint8_data1 = funcCode & 0xFF;
  uint8_t uint8_data2 = minioCode & 0xFF;
  uint8_t uint8_data3 = extra1 & 0xFF;
  uint8_t uint8_data4 = extra2 & 0xFF;

  return (uint8_data4 << 24) | (uint8_data3 << 16) | (uint8_data2 << 8) | uint8_data1;
}

uint64_t timestampDiff(struct timespec *t1, struct timespec *t2) {
  uint64_t difference_ms;

  difference_ms = (t1->tv_sec - t2->tv_sec) * 1000;

  // Calculate difference in nanoseconds (considering overflow)
  if (t1->tv_nsec >= t2->tv_nsec) {
    difference_ms += (t1->tv_nsec - t2->tv_nsec) / 1000000;
  } else {
    // Handle potential overflow (t2 might be larger)
    difference_ms += ((t1->tv_nsec + 1000000000) - t2->tv_nsec) / 1000000;
    difference_ms--; // Adjust for the borrowed second
  }

  printf("loop diff: %lu", difference_ms);
  return difference_ms;
}
