#include "custom_error.h"
#include "rdma_common.h"
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/poll.h>
#include <sys/select.h>
#include <sys/socket.h>

// NOTES
// -- WE CAN REGISTER MANY BUFFERS IN ONE SEND USING THE SGE LIST.
// -- MUST ACK ALL EVENTS BEFORE DESTROYING CM_ID

// ibv_mr == MEMORY REGION
// ibv_sge = Scatter Gather Entry
// ibv_recv_wr == receive work request
// --- CAN HAVE MANY SGES IN LIST.

// create event channel
// create cm_id
// resolve addr -- wait for ADDR_RESOLVED + ACK
// resolve route -- wait for ROUTE_RESOLVED + ACK
// create PD
// create Completion channel
// create QC + notify settings
// create QP
//
//
////// META HANDSHAKE ????
// create server metadata buffer
//  ibv_mr ( rdma_buffer_attr )
//  ibv_sge.addr = ibv_mr.addr
//  ibv_sge.length = ibv_mr.length
//  ibv_sge.lkey = ibv_mr.lkey
//  ibv_post_recv(QP, recv_wr, bad_recv_wr)
//
//

typedef struct {
  struct rdma_event_channel *EventChannel;
  struct rdma_cm_id *cm_id;
  struct sockaddr_in addr;
  // struct sockaddr addr;

} client;
client *clients[1000];

uint32_t createClient(char *addr, char *port) {
  //
  return 0;
}

uint32_t createEventChannel() {
  struct rdma_event_channel *cm_event_channel = rdma_create_event_channel();
  if (!cm_event_channel) {
    return makeError(0, ErrUnableToCreateEventChannel, 0, 0);
  }

  return 0;
}

uint32_t setEventChannelToNoneBlocking(int clientIndex) {
  client *c = clients[clientIndex];
  int fd = c->EventChannel->fd;
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    return makeError(flags, ErrUnableToGetEventChannelFlags, 0, 0);
  }
  int16_t ret = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  if (ret == -1) {
    return makeError(ret, ErrUnableToSetEventChannelToNoneBlocking, 0, 0);
  }

  return 0;
}

uint32_t resolveAddress(int clientIndex) {
  client *c = clients[clientIndex];
  uint16_t ret = rdma_resolve_addr(
      c->cm_id,
      NULL,
      (struct sockaddr *)&c->addr,
      2000);
  if (ret) {
    return makeError(ret, ErrUnableToResolveAddress, 0, 0);
  }

  return 0;
}

////
////
////
////
////
////
////
////
////
////
////
////
////
////
////
////
////
////
////
////
////
