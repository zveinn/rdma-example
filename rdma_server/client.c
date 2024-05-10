#include "rdma_common.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
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
  int index;
  struct sockaddr_in addr;
  enum rdma_port_space port_space;
  struct rdma_conn_param RDMAConnectionParameters;
  int RDMAConnectionInitiatorDepth;
  int RDMAConnectionResponderResources;
  int RDMAConnectionRetryCount;

  int route_resolve_timeout;
  int addr_resolve_timeout;

  struct rdma_event_channel *EventChannel;
  struct rdma_cm_id *CMID;

  int CompletionQueueCapacity;
  int MaxReceiveSGE;
  int MaxReceiveWR;
  int MaxSendSGE;
  int MaxSendWR;
  struct ibv_comp_channel *CompletionChannel;
  struct ibv_cq *CompletionQueue;
  struct ibv_qp_init_attr QueuePairAttr;
  // IBV_QPT_RC
  enum ibv_qp_type QueuePairType;

  struct ibv_pd *ProtectedDomain;

  // LOCAL META DATA
  struct rdma_buffer_attr LocalMetaAttributes;
  struct ibv_mr *LocalMetaMR;
  struct ibv_mr *LocalSourceMR;
  struct ibv_mr *LocalSourceMR2;
  struct ibv_sge LocalSendSGE;
  struct ibv_recv_wr LocalReceiveWR;
  struct ibv_recv_wr *BadLocalReceiveWR;
  struct ibv_send_wr LocalSendWR;
  struct ibv_send_wr *BadLocalSendWR;

  // REMOTE META DATA
  struct rdma_buffer_attr RemoteMetaAttributes;
  struct ibv_mr *RemoteMetaMR;
  struct ibv_sge RemoteMetaSGE;
  struct ibv_recv_wr RemoteMetaReceiveWR;
  struct ibv_recv_wr *BadRemoteMetaReceiveWR;

  struct rdma_buffer_attr RemoteMetaAttributes2;
  struct ibv_mr *RemoteMetaMR2;
  struct ibv_sge RemoteMetaSGE2;
  struct ibv_recv_wr RemoteMetaReceiveWR2;
  struct ibv_recv_wr *BadRemoteMetaReceiveWR2;

} client;
client *clients[1000];

uint32_t createClient(
    int clientIndex,
    char *addr,
    char *port,
    int addr_resolve_timeout,
    int route_resolve_timeout,
    int completion_queue_capacity,
    int MaxReceiveWR,
    int MaxSendWR,
    int MaxReceiveSGE,
    int MaxSendSGE,
    enum ibv_qp_type queue_pair_type,
    enum rdma_port_space port_space) {

  clients[clientIndex] = malloc(sizeof(client));
  clients[clientIndex]->port_space = port_space;
  clients[clientIndex]->QueuePairType = queue_pair_type;
  clients[clientIndex]->CompletionQueueCapacity = completion_queue_capacity;

  // clients[clientIndex]->addr = addr;
  clients[clientIndex]->addr_resolve_timeout = addr_resolve_timeout;
  clients[clientIndex]->route_resolve_timeout = route_resolve_timeout;
  clients[clientIndex]->port_space = port_space;
  clients[clientIndex]->MaxSendWR = MaxSendWR;
  clients[clientIndex]->MaxSendSGE = MaxSendSGE;
  clients[clientIndex]->MaxReceiveWR = MaxReceiveWR;
  clients[clientIndex]->MaxReceiveSGE = MaxReceiveSGE;

  // clients[clientIndex].addr = malloc(sizeof(struct sockaddr_in));
  struct sockaddr_in server_sockaddr;
  bzero(&server_sockaddr, sizeof server_sockaddr);
  server_sockaddr.sin_family = AF_INET;
  server_sockaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  int ret;
  ret = get_addr(addr, (struct sockaddr *)&server_sockaddr);
  server_sockaddr.sin_port = htons(strtol(port, NULL, 0));
  clients[clientIndex]->addr = server_sockaddr;
  printf("ADDR RET: %d\n", ret);
  printf("UP: %p\n", clients[clientIndex]);
  return 0;
}

uint32_t getClient(int clientIndex, client **client) {
  if (clientIndex > MaxClients) {
    return makeError(0, ErrClientIndexOutOfBounds, clientIndex, 0);
  }
  *client = clients[clientIndex];
  if (!client) {
    return makeError(0, ErrUnableToGetClient, clientIndex, 0);
  }
  return 0;
}

uint32_t deleteClient(int clientIndex) {
  client *c;
  uint32_t cErr = getClient(clientIndex, &c);
  if (cErr) {
    return cErr;
  }

  free(c);
  clients[clientIndex] = NULL;

  return 0;
}

uint32_t createEventChannel(int clientIndex) {
  client *c = NULL;
  uint32_t cErr = getClient(clientIndex, &c);
  if (cErr) {
    return cErr;
  }

  c->EventChannel = rdma_create_event_channel();
  if (!c->EventChannel) {
    return makeError(0, ErrUnableToCreateEventChannel, 0, 0);
  }

  return 0;
}

uint32_t setEventChannelToNoneBlocking(int clientIndex) {
  client *c = NULL;
  uint32_t cErr = getClient(clientIndex, &c);
  if (cErr) {
    return cErr;
  }

  int flags = fcntl(c->EventChannel->fd, F_GETFL, 0);
  if (flags == -1) {
    return makeError(flags, ErrUnableToGetEventChannelFlags, 0, 0);
  }

  int8_t ret = fcntl(
      c->EventChannel->fd,
      F_SETFL,
      flags | O_NONBLOCK);

  if (ret == -1) {
    return makeError(ret, ErrUnableToSetEventChannelToNoneBlocking, 0, 0);
  }

  return 0;
}

uint32_t RDMACreateID(int clientIndex) {
  client *c = NULL;
  uint32_t cErr = getClient(clientIndex, &c);
  if (cErr) {
    return cErr;
  }

  int8_t ret = rdma_create_id(
      c->EventChannel,
      &c->CMID,
      NULL,
      c->port_space);

  if (ret) {
    return makeError(ret, ErrUnableToCreateCMID, 0, 0);
  }

  return 0;
}

uint32_t RDMAResolveAddress(int clientIndex) {
  client *c = NULL;
  uint32_t cErr = getClient(clientIndex, &c);
  if (cErr) {
    return cErr;
  }

  uint16_t ret = rdma_resolve_addr(
      c->CMID,
      NULL,
      (struct sockaddr *)&c->addr,
      c->addr_resolve_timeout);

  if (ret) {
    return makeError(ret, ErrUnableToResolveAddress, 0, 0);
  }

  return 0;
}

uint32_t RDMAResolveRoute(int clientIndex) {
  client *c = NULL;
  uint32_t cErr = getClient(clientIndex, &c);
  if (cErr) {
    return cErr;
  }

  uint16_t ret = rdma_resolve_route(
      c->CMID,
      c->route_resolve_timeout);

  if (ret) {
    return makeError(ret, ErrUnableToResolveAddress, 0, 0);
  }

  return 0;
}

uint32_t IBVAllocProtectedDomain(int clientIndex) {
  client *c = NULL;
  uint32_t cErr = getClient(clientIndex, &c);
  if (cErr) {
    return cErr;
  }

  c->ProtectedDomain = ibv_alloc_pd(c->CMID->verbs);
  if (!c->ProtectedDomain) {
    return makeError(0, ErrUnableToAllocatePD, 0, 0);
  }

  return 0;
}

uint32_t IBVCreateCompletionChannel(int clientIndex) {
  client *c = NULL;
  uint32_t cErr = getClient(clientIndex, &c);
  if (cErr) {
    return cErr;
  }

  c->CompletionChannel = ibv_create_comp_channel(c->CMID->verbs);

  if (!c->CompletionChannel) {
    return makeError(0, ErrUnableToCreateCompletionChannel, 0, 0);
  }

  return 0;
}

uint32_t IBVCreateCompletionQueue(int clientIndex) {
  client *c = NULL;
  uint32_t cErr = getClient(clientIndex, &c);
  if (cErr) {
    return cErr;
  }

  c->CompletionQueue = ibv_create_cq(
      c->CMID->verbs,
      c->CompletionQueueCapacity,
      NULL,
      c->CompletionChannel,
      0);

  if (!c->CompletionQueue) {
    return makeError(0, ErrUnableToCreateCompletionQueue, 0, 0);
  }

  int8_t ret = ibv_req_notify_cq(
      c->CompletionQueue,
      0);
  if (ret) {
    return makeError(ret, ErrUnableToRegisterCQNotifications, 0, 0);
  }

  return 0;
}

uint32_t RDMACreateQueuePairs(int clientIndex) {
  client *c = NULL;
  uint32_t cErr = getClient(clientIndex, &c);
  if (cErr) {
    return cErr;
  }

  // bzero(&c->QueuePairAttr, sizeof c->QueuePairAttr);
  c->QueuePairAttr.cap.max_recv_sge = c->MaxReceiveSGE;
  c->QueuePairAttr.cap.max_recv_wr = c->MaxReceiveWR;
  c->QueuePairAttr.cap.max_send_sge = c->MaxSendSGE;
  c->QueuePairAttr.cap.max_send_wr = c->MaxSendWR;
  c->QueuePairAttr.qp_type = c->QueuePairType;
  c->QueuePairAttr.recv_cq = c->CompletionQueue;
  c->QueuePairAttr.send_cq = c->CompletionQueue;
  int8_t ret = rdma_create_qp(c->CMID,
                              c->ProtectedDomain,
                              &c->QueuePairAttr);
  if (ret) {
    return makeError(0, ErrUnableToCreateQueuePairs, 0, 0);
  }

  return 0;
}
// This is the buffer which the remote server will
// write metra attributes to.
uint32_t RegisterBufferForRemoteMetaAttributes2(int clientIndex) {
  client *c = NULL;
  uint32_t cErr = getClient(clientIndex, &c);
  if (cErr) {
    return cErr;
  }

  c->RemoteMetaMR2 = rdma_buffer_register(
      c->ProtectedDomain,
      &c->RemoteMetaAttributes2,
      sizeof(c->RemoteMetaAttributes2),
      (IBV_ACCESS_LOCAL_WRITE));

  if (!c->RemoteMetaMR2) {
    return makeError(0, ErrUnableToCreateMetaMR, 0, 0);
  }

  c->RemoteMetaSGE2.addr = (uint64_t)c->RemoteMetaMR2->addr;
  c->RemoteMetaSGE2.length = c->RemoteMetaMR2->length;
  c->RemoteMetaSGE2.lkey = c->RemoteMetaMR2->lkey;

  c->RemoteMetaReceiveWR2.sg_list = &c->RemoteMetaSGE2;
  c->RemoteMetaReceiveWR2.num_sge = 1;

  int8_t ret = ibv_post_recv(c->CMID->qp, &c->RemoteMetaReceiveWR2, &c->BadRemoteMetaReceiveWR2);
  if (ret) {
    printf("FAIL REGISTER\n");
    return makeError(0, ErrUnableToPostRemoteMetaMRToReceiveQueue, 0, 0);
  }

  return 0;
}

// This is the buffer which the remote server will
// write metra attributes to.
uint32_t RegisterBufferForRemoteMetaAttributes(int clientIndex) {
  client *c = NULL;
  uint32_t cErr = getClient(clientIndex, &c);
  if (cErr) {
    return cErr;
  }

  c->RemoteMetaMR = rdma_buffer_register(
      c->ProtectedDomain,
      &c->RemoteMetaAttributes,
      sizeof(c->RemoteMetaAttributes),
      (IBV_ACCESS_LOCAL_WRITE));

  if (!c->RemoteMetaMR) {
    return makeError(0, ErrUnableToCreateMetaMR, 0, 0);
  }

  c->RemoteMetaSGE.addr = (uint64_t)c->RemoteMetaMR->addr;
  c->RemoteMetaSGE.length = c->RemoteMetaMR->length;
  c->RemoteMetaSGE.lkey = c->RemoteMetaMR->lkey;

  c->RemoteMetaReceiveWR.sg_list = &c->RemoteMetaSGE;
  c->RemoteMetaReceiveWR.num_sge = 1;

  int8_t ret = ibv_post_recv(c->CMID->qp, &c->RemoteMetaReceiveWR, &c->BadRemoteMetaReceiveWR);
  if (ret) {
    printf("FAIL REGISTER\n");
    return makeError(0, ErrUnableToPostRemoteMetaMRToReceiveQueue, 0, 0);
  }

  return 0;
}

uint32_t RDMAConnect(int clientIndex) {
  client *c = NULL;
  uint32_t cErr = getClient(clientIndex, &c);
  if (cErr) {
    return cErr;
  }

  c->RDMAConnectionParameters.initiator_depth = c->RDMAConnectionInitiatorDepth;
  c->RDMAConnectionParameters.responder_resources = c->RDMAConnectionResponderResources;
  c->RDMAConnectionParameters.retry_count = c->RDMAConnectionRetryCount;

  uint16_t ret = rdma_connect(
      c->CMID,
      &c->RDMAConnectionParameters);

  if (ret) {
    return makeError(ret, ErrUnableToResolveAddress, 0, 0);
  }

  return 0;
}

uint32_t RegisterLocalBufferAtRemoteServer(int clientIndex, char *buffer) {
  client *c = NULL;
  uint32_t cErr = getClient(clientIndex, &c);
  if (cErr) {
    return cErr;
  }

  printf("buffer: %s\n", buffer);
  printf("buffer: %p\n", buffer);
  // printf("buffer: %p\n", &buffer);
  c->LocalSourceMR = rdma_buffer_register(
      c->ProtectedDomain,
      buffer,
      strlen(buffer),
      (IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ |
       IBV_ACCESS_REMOTE_WRITE));

  if (!c->LocalSourceMR) {
    return makeError(0, ErrUnableToCreateMetaMR, 0, 0);
  }

  printf("1\n");
  c->LocalMetaAttributes.address = (uint64_t)c->LocalSourceMR->addr;
  c->LocalMetaAttributes.length = (uint64_t)c->LocalSourceMR->length;
  c->LocalMetaAttributes.stag.local_stag = (uint64_t)c->LocalSourceMR->lkey;

  printf("2\n");
  c->LocalMetaMR = rdma_buffer_register(
      c->ProtectedDomain,
      &c->LocalMetaAttributes,
      sizeof(c->LocalMetaAttributes),
      (IBV_ACCESS_LOCAL_WRITE));

  if (!c->LocalMetaMR) {
    return makeError(0, 0255, 0, 0);
  }

  printf("3\n");
  c->LocalSendSGE.addr = (uint64_t)c->LocalMetaMR->addr;
  c->LocalSendSGE.length = (uint64_t)c->LocalMetaMR->length;
  c->LocalSendSGE.lkey = (uint64_t)c->LocalMetaMR->lkey;

  printf("4\n");
  bzero(&c->LocalSendWR, sizeof(c->LocalSendWR));
  c->LocalSendWR.sg_list = &c->LocalSendSGE;
  c->LocalSendWR.num_sge = 1;
  c->LocalSendWR.opcode = IBV_WR_SEND;
  c->LocalSendWR.send_flags = IBV_SEND_SIGNALED;

  printf("5\n");
  int ret = ibv_post_send(c->CMID->qp, &c->LocalSendWR, &c->BadLocalSendWR);
  if (ret) {
    return makeError(ret, 0255, 0, 0);
  }
  printf("6\n");
  return 0;
}

uint32_t WriteToRemoteBuffer(int clientIndex) {
  client *c = NULL;
  uint32_t cErr = getClient(clientIndex, &c);
  if (cErr) {
    return cErr;
  }
  printf("1\n");
  c->LocalSendSGE.addr = (uint64_t)c->LocalSourceMR->addr;
  c->LocalSendSGE.length = (uint64_t)c->LocalSourceMR->length;
  c->LocalSendSGE.lkey = (uint64_t)c->LocalSourceMR->lkey;

  printf("2\n");
  // bzero(&c->LocalSendWR, sizeof(c->LocalSendWR));
  c->LocalSendWR.sg_list = &c->LocalSendSGE;
  c->LocalSendWR.num_sge = 1;
  c->LocalSendWR.opcode = IBV_WR_RDMA_WRITE;
  c->LocalSendWR.send_flags = IBV_SEND_SIGNALED;

  c->LocalSendWR.wr.rdma.rkey = c->RemoteMetaAttributes.stag.remote_stag;
  c->LocalSendWR.wr.rdma.remote_addr = c->RemoteMetaAttributes.address;

  printf("3\n");
  int ret = ibv_post_send(c->CMID->qp, &c->LocalSendWR, &c->BadLocalSendWR);
  if (ret) {
    return makeError(ret, 0255, 0, 0);
  }
  printf("4\n");

  return 0;
}
uint32_t WriteToRemoteBuffer2(int clientIndex) {
  client *c = NULL;
  uint32_t cErr = getClient(clientIndex, &c);
  if (cErr) {
    return cErr;
  }

  printf("1\n");
  static char *src = NULL;
  src = calloc(7, 1);
  strncpy(src, "hello2!", 7);
  printf("BuFFER: %p\n", src);
  c->LocalSourceMR2 = rdma_buffer_register(
      c->ProtectedDomain,
      src,
      strlen(src),
      (IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ |
       IBV_ACCESS_REMOTE_WRITE));

  if (!c->LocalSourceMR2) {
    return makeError(0, ErrUnableToCreateMetaMR, 0, 0);
  }
  c->LocalSendSGE.addr = (uint64_t)c->LocalSourceMR2->addr;
  c->LocalSendSGE.length = (uint64_t)c->LocalSourceMR2->length;
  c->LocalSendSGE.lkey = (uint64_t)c->LocalSourceMR2->lkey;

  printf("2\n");
  // bzero(&c->LocalSendWR, sizeof(c->LocalSendWR));
  c->LocalSendWR.sg_list = &c->LocalSendSGE;
  c->LocalSendWR.num_sge = 1;
  c->LocalSendWR.opcode = IBV_WR_RDMA_WRITE;
  c->LocalSendWR.send_flags = IBV_SEND_SIGNALED;

  c->LocalSendWR.wr.rdma.rkey = c->RemoteMetaAttributes.stag.remote_stag;
  c->LocalSendWR.wr.rdma.remote_addr = c->RemoteMetaAttributes.address;

  printf("3\n");
  int ret = ibv_post_send(c->CMID->qp, &c->LocalSendWR, &c->BadLocalSendWR);
  if (ret) {
    return makeError(ret, 0255, 0, 0);
  }
  printf("4\n");

  return 0;
}

//// TODO ... EXCHANGE META INFO
//// TOTO .. CLIENT WRITE
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
///
int main() {

  // char *src = calloc(2000, 1);
  // printf("BuFFER: %p\n", &src);
  static char *src = NULL;
  src = calloc(5, 1);
  strncpy(src, "hello", 5);
  printf("BuFFER: %p\n", src);

  uint32_t ret;
  ret = createClient(
      1,
      "15.15.15.2",
      "11111",
      5000, 5000,
      200,
      10, 10, 10, 10,
      IBV_QPT_RC,
      RDMA_PS_TCP);

  printf("1: 0x%X\n", ret);
  ret = createEventChannel(1);
  printf("2: 0x%X\n", ret);
  ret = setEventChannelToNoneBlocking(1);
  printf("3: 0x%X\n", ret);
  ret = RDMACreateID(1);
  printf("4: 0x%X\n", ret);
  ret = RDMAResolveAddress(1);
  printf("5: 0x%X\n", ret);

  struct rdma_cm_event *cm_event = NULL;
  client *c;
  ret = getClient(1, &c);
  printf("6: 0x%X\n", ret);
  ret = pollEventChannel(
      c->EventChannel,
      RDMA_CM_EVENT_ADDR_RESOLVED,
      0,
      3000,
      &cm_event);

  printf("7: 0x%X\n", ret);
  ret = RDMAResolveRoute(1);
  printf("8: 0x%X\n", ret);
  ret = pollEventChannel(
      c->EventChannel,
      RDMA_CM_EVENT_ROUTE_RESOLVED,
      0,
      3000,
      &cm_event);

  printf("9: 0x%X\n", ret);
  ret = IBVAllocProtectedDomain(1);
  printf("10: 0x%X\n", ret);
  ret = IBVCreateCompletionChannel(1);
  printf("11: 0x%X\n", ret);
  ret = IBVCreateCompletionQueue(1);
  printf("12: 0x%X\n", ret);
  ret = RDMACreateQueuePairs(1);
  printf("13: 0x%X\n", ret);
  ret = RegisterBufferForRemoteMetaAttributes(1);
  ret = RegisterBufferForRemoteMetaAttributes2(1);
  printf("14: 0x%X\n", ret);
  show_rdma_buffer_attr(&c->RemoteMetaAttributes);
  ret = RDMAConnect(1);
  printf("15: 0x%X\n", ret);
  show_rdma_buffer_attr(&c->RemoteMetaAttributes);
  ret = pollEventChannel(
      c->EventChannel,
      RDMA_CM_EVENT_ESTABLISHED,
      0,
      3000,
      &cm_event);

  printf("16: 0x%X\n", ret);
  show_rdma_buffer_attr(&c->RemoteMetaAttributes);
  ret = RegisterLocalBufferAtRemoteServer(1, src);
  printf("17: 0x%X\n", ret);
  show_rdma_buffer_attr(&c->RemoteMetaAttributes);

  struct ibv_wc wc[2];
  printf("WAIT1....\n");
  ret = process_work_completion_events(c->CompletionChannel, wc);
  if (ret != 1) {
    printf("FAILED1\n");
    return ret;
  }
  show_rdma_buffer_attr(&c->RemoteMetaAttributes);

  printf("WAIT2....\n");
  ret = process_work_completion_events(c->CompletionChannel, wc);
  if (ret != 1) {
    printf("FAILED2\n");
    return ret;
  }
  show_rdma_buffer_attr(&c->RemoteMetaAttributes);
  // printf("NAILED IT: 0x%X\n", ret);

  printf("18: 0x%X\n", ret);
  ret = WriteToRemoteBuffer(1);
  printf("19: 0x%X\n", ret);
  sleep(5);
  ret = WriteToRemoteBuffer2(1);
  printf("20: 0x%X\n", ret);

  while (1) {
    show_rdma_buffer_attr(&c->RemoteMetaAttributes);
    show_rdma_buffer_attr(&c->RemoteMetaAttributes2);
    show_rdma_buffer_attr(&c->LocalMetaAttributes);
    printf("MADE IT!\n");
    sleep(1);
  }

  return 1;
}
