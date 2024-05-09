#include "rdma_common.h"
#include <stdint.h>
#include <stdio.h>
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
  struct ibv_qp_init_attr QueuePairs;
  // IBV_QPT_RC
  enum ibv_qp_type QueuePairType;

  struct ibv_pd *ProtectedDomain;

  // LOCAL META DATA
  struct rdma_buffer_attr LocalMetaAttributes;
  struct ibv_mr *LocalMetaMR;
  struct ibv_mr *LocalSourceMR;
  struct ibv_sge LocalMetaSGE;
  struct ibv_recv_wr LocalMetaReceiveWR;
  struct ibv_recv_wr *BadLocalMetaReceiveWR;
  struct ibv_send_wr LocalMetaSendWR;
  struct ibv_send_wr *BadLocalMetaSendWR;

  // REMOTE META DATA
  struct rdma_buffer_attr RemoteMetaAttributes;
  struct ibv_mr *RemoteMetaMR;
  struct ibv_sge RemoteMetaSGE;
  struct ibv_recv_wr RemoteMetaReceiveWR;
  struct ibv_recv_wr *BadRemoteMetaReceiveWR;

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

  printf("CLIENT %p!\n", c);
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

  // bzero(&qp_init_attr, sizeof qp_init_attr);
  c->QueuePairs.cap.max_recv_sge = c->MaxReceiveSGE;
  c->QueuePairs.cap.max_recv_wr = c->MaxReceiveWR;
  c->QueuePairs.cap.max_send_sge = c->MaxSendSGE;
  c->QueuePairs.cap.max_send_wr = c->MaxSendWR;
  c->QueuePairs.qp_type = c->QueuePairType;
  c->QueuePairs.recv_cq = c->CompletionQueue;
  c->QueuePairs.send_cq = c->CompletionQueue;
  int8_t ret = rdma_create_qp(c->CMID,
                              c->ProtectedDomain,
                              &c->QueuePairs);
  if (ret) {
    return makeError(0, ErrUnableToCreateQueuePairs, 0, 0);
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
      sizeof(c->RemoteMetaAttributes), (IBV_ACCESS_LOCAL_WRITE));

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

  c->LocalSourceMR = rdma_buffer_register(
      c->ProtectedDomain,
      &buffer,
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
  c->LocalMetaMR = rdma_buffer_register(c->ProtectedDomain, &c->LocalMetaAttributes, sizeof(c->LocalMetaAttributes), (IBV_ACCESS_LOCAL_WRITE));
  if (!c->LocalMetaMR) {
    return makeError(0, 0255, 0, 0);
  }

  printf("3\n");
  c->LocalMetaSGE.addr = (uint64_t)c->LocalMetaMR->addr;
  c->LocalMetaSGE.length = (uint64_t)c->LocalMetaMR->length;
  c->LocalMetaSGE.lkey = (uint64_t)c->LocalMetaMR->lkey;

  printf("4\n");
  bzero(&c->LocalMetaSendWR, sizeof(c->LocalMetaSendWR));
  c->LocalMetaSendWR.sg_list = &c->LocalMetaSGE;
  c->LocalMetaSendWR.num_sge = 1;
  c->LocalMetaSendWR.opcode = IBV_WR_SEND;
  c->LocalMetaSendWR.send_flags = IBV_SEND_SIGNALED;

  printf("5\n");
  int ret = ibv_post_send(c->CMID->qp, &c->LocalMetaSendWR, &c->BadLocalMetaSendWR);
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
  c->LocalMetaSGE.addr = (uint64_t)c->LocalSourceMR->addr;
  c->LocalMetaSGE.length = (uint64_t)c->LocalSourceMR->length;
  c->LocalMetaSGE.lkey = (uint64_t)c->LocalSourceMR->lkey;

  printf("2\n");
  bzero(&c->LocalMetaSendWR, sizeof(c->LocalMetaSendWR));
  c->LocalMetaSendWR.sg_list = &c->LocalMetaSGE;
  c->LocalMetaSendWR.num_sge = 1;
  c->LocalMetaSendWR.opcode = IBV_WR_RDMA_WRITE;
  c->LocalMetaSendWR.send_flags = IBV_SEND_SIGNALED;

  c->LocalMetaSendWR.wr.rdma.rkey = c->RemoteMetaAttributes.stag.remote_stag;
  c->LocalMetaSendWR.wr.rdma.remote_addr = c->RemoteMetaAttributes.address;

  printf("3\n");
  int ret = ibv_post_send(c->CMID->qp, &c->LocalMetaSendWR, &c->BadLocalMetaSendWR);
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
  char *src = "01234567890123456789012345678901234567890123456789";

  uint32_t ret;
  ret = createClient(
      1,
      "15.15.15.2",
      "11111",
      5000, 5000,
      10000,
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
  ret = RDMACreateQueuePairs(1);
  printf("12: 0x%X\n", ret);
  ret = RegisterBufferForRemoteMetaAttributes(1);
  printf("13: 0x%X\n", ret);
  ret = RDMAConnect(1);
  printf("14: 0x%X\n", ret);
  ret = pollEventChannel(
      c->EventChannel,
      RDMA_CM_EVENT_ESTABLISHED,
      0,
      3000,
      &cm_event);

  printf("15: 0x%X\n", ret);
  ret = RegisterLocalBufferAtRemoteServer(1, src);
  printf("16: 0x%X\n", ret);

  struct ibv_wc wc[2];
  ret = process_work_completion_events(c->CompletionChannel, wc);
  if (ret != 1) {
    debug("We failed to get work completions , ret = %d \n", ret);
    return ret;
  }
  printf("17: 0x%X\n", ret);

  ret = process_work_completion_events(c->CompletionChannel, wc);
  if (ret != 1) {
    debug("We failed to get work completions , ret = %d \n", ret);
    return ret;
  }

  printf("18: 0x%X\n", ret);
  ret = WriteToRemoteBuffer(1);
  printf("19: 0x%X\n", ret);

  while (1) {
    printf("MADE IT!\n");
    sleep(1);
  }

  return 1;
}
