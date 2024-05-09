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
  struct ibv_sge *LocalMetaSGE;
  struct ibv_recv_wr *LocalMetaReceiveWR;
  struct ibv_recv_wr *BadLocalMetaReceiveWR;

  // REMOTE META DATA
  struct rdma_buffer_attr RemoteMetaAttributes;
  struct ibv_mr *RemoteMetaMR;
  struct ibv_sge *RemoteMetaSGE;
  struct ibv_recv_wr *RemoteMetaReceiveWR;
  struct ibv_recv_wr *BadRemoteMetaReceiveWR;

} client;
client *clients[1000];

uint32_t createClient(
    int clientIndex,
    char *addr,
    char *port,
    int addr_resolve_timeout,
    int route_resolve_timeout,
    enum rdma_port_space port_space) {

  clients[clientIndex] = malloc(sizeof(client));
  clients[clientIndex]->port_space = port_space;
  // clients[clientIndex]->addr = addr;
  clients[clientIndex]->addr_resolve_timeout = addr_resolve_timeout;
  clients[clientIndex]->route_resolve_timeout = route_resolve_timeout;
  clients[clientIndex]->port_space = port_space;

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

uint32_t getClient(int clientIndex, client *client) {
  if (clientIndex > MaxClients) {
    return makeError(0, ErrClientIndexOutOfBounds, clientIndex, 0);
  }
  printf("CID: %d\n", clientIndex);
  client = *&clients[clientIndex];
  if (!client) {
    return makeError(0, ErrUnableToGetClient, clientIndex, 0);
  }
  printf("cok: %d\n", clientIndex);
  printf("cok: %p\n", client);
  return 0;
}

uint32_t deleteClient(int clientIndex) {
  client *c;
  uint32_t cErr = getClient(clientIndex, c);
  if (cErr) {
    return cErr;
  }

  free(c);
  clients[clientIndex] = NULL;

  return 0;
}

uint32_t createEventChannel(int clientIndex) {
  client *c;
  uint32_t cErr = getClient(clientIndex, c);
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
  uint32_t cErr = getClient(clientIndex, c);
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
  uint32_t cErr = getClient(clientIndex, c);
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
  uint32_t cErr = getClient(clientIndex, c);
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
  uint32_t cErr = getClient(clientIndex, c);
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
  uint32_t cErr = getClient(clientIndex, c);
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
  uint32_t cErr = getClient(clientIndex, c);
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
  uint32_t cErr = getClient(clientIndex, c);
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

  return 0;
}

uint32_t IBVRequestCompletionNotifications(int clientIndex) {
  client *c = NULL;
  uint32_t cErr = getClient(clientIndex, c);
  if (cErr) {
    return cErr;
  }

  int8_t ret = ibv_req_notify_cq(
      c->CompletionQueue,
      0);

  if (ret) {
    return makeError(0, ErrUnableToRegisterCQNotifications, 0, 0);
  }

  return 0;
}

uint32_t RDMACreateQueuePairs(int clientIndex) {
  client *c = NULL;
  uint32_t cErr = getClient(clientIndex, c);
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
  uint32_t cErr = getClient(clientIndex, c);
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

  c->RemoteMetaSGE->addr = (uint64_t)c->RemoteMetaMR->addr;
  c->RemoteMetaSGE->length = c->RemoteMetaMR->length;
  c->RemoteMetaSGE->lkey = c->RemoteMetaMR->lkey;

  c->RemoteMetaReceiveWR->sg_list = c->RemoteMetaSGE;
  c->RemoteMetaReceiveWR->num_sge = 1;

  int8_t ret = ibv_post_recv(c->CMID->qp, c->RemoteMetaReceiveWR, &c->BadRemoteMetaReceiveWR);
  if (ret) {
    return makeError(0, ErrUnableToPostRemoteMetaMRToReceiveQueue, 0, 0);
  }

  return 0;
}

uint32_t RDMAConnect(int clientIndex) {
  client *c = NULL;
  uint32_t cErr = getClient(clientIndex, c);
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

  struct rdma_cm_event *cm_event = NULL;
  uint32_t retPoll = pollEventChannel(
      c->EventChannel,
      RDMA_CM_EVENT_ESTABLISHED,
      0,
      3000,
      &cm_event);
  if (ret) {
    return ret;
  }

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
  uint32_t ret;
  ret = createClient(
      1,
      "15.15.15.2",
      "11111",
      5000,
      5000,
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
  ret = getClient(1, c);
  printf("6: 0x%X\n", ret);
  ret = pollEventChannel(
      c->EventChannel,
      RDMA_CM_EVENT_ADDR_RESOLVED,
      0,
      3000,
      &cm_event);

  ret = RDMAResolveRoute(1);
  printf("7: 0x%X\n", ret);
  ret = pollEventChannel(
      c->EventChannel,
      RDMA_CM_EVENT_ADDR_RESOLVED,
      0,
      3000,
      &cm_event);
  printf("8: 0x%X\n", ret);
  printf("9: 0x%X\n", ret);
  printf("10: 0x%X\n", ret);
  printf("11: 0x%X\n", ret);
  printf("12: 0x%X\n", ret);
  printf("13: 0x%X\n", ret);

  return 1;
}
