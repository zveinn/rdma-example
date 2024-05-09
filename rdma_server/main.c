#include "rdma_common.h"

void usage() {
  printf("Usage:\n");
  printf("rdma_server: [-a <server_addr>] [-p <server_port>]\n");
  printf("(default port is %d)\n", DEFAULT_RDMA_PORT);
  exit(1);
}
// RDMA CONSTANTS
static struct rdma_event_channel *EventChannel = NULL;
static struct rdma_cm_id *serverID = NULL;

typedef struct {
  struct rdma_conn_param conn_param;

  struct rdma_cm_event *cm_event;
  struct rdma_cm_id *cm_event_id;
  struct ibv_comp_channel *completionChannel;
  struct ibv_pd *PD;
  struct ibv_cq *CQ;
  struct ibv_qp_init_attr QP;

  // META???
  struct ibv_mr *metaMR;
  struct rdma_buffer_attr metaAttr;

  struct ibv_mr *serverMR;
  struct ibv_mr *serverMetaMR;
  // struct ibv_mr *SERVER_MR;
  // struct ibv_mr *SERVER_BUFFER_MR;
  struct ibv_sge SGE;
  struct ibv_recv_wr RCV_WR;
  struct ibv_recv_wr *BAD_RCV_WR;

  struct ibv_sge Server_SGE;
  struct ibv_send_wr Server_RCV_WR;
  struct ibv_send_wr *Server_BAD_RCV_WR;
  // struct ibv_recv_wr RCV_WR;

  struct rdma_buffer_attr Server_B2;
  struct rdma_buffer_attr B3;
  struct rdma_buffer_attr B4;
  char *dataBuffer;

  int index;
} connection;
const int MaxConnections = 10000;
connection *connections[10000];

static int createQueuePairs(connection *c) {
  int ret = -1;
  bzero(&c->QP, sizeof c->QP);
  c->QP.cap.max_recv_sge = MAX_SGE;
  c->QP.cap.max_recv_wr = MAX_WR;
  c->QP.cap.max_send_sge = MAX_SGE;
  c->QP.cap.max_send_wr = MAX_WR;
  c->QP.qp_type = IBV_QPT_RC;
  c->QP.recv_cq = c->CQ;
  c->QP.send_cq = c->CQ;
  ret = rdma_create_qp(c->cm_event_id, c->PD, &c->QP);
  if (ret) {
    return ErrUnableToCreateQueuePairs;
  }
  return ret;
}

static int setup_client_resources(connection *c) {
  int ret = -1;

  c->PD = ibv_alloc_pd(c->cm_event_id->verbs);
  if (!c->PD) {
    return ErrUnableToAllocatePD;
  }

  c->completionChannel = ibv_create_comp_channel(c->cm_event_id->verbs);
  if (!c->completionChannel) {
    return ErrUnableToCreateCompletionChannel;
  }

  c->CQ = ibv_create_cq(c->cm_event_id->verbs, CQ_CAPACITY, NULL,
                        c->completionChannel, 0);
  if (!c->CQ) {
    return ErrUnableToCreateCompletionQueue;
  }

  ret = ibv_req_notify_cq(c->CQ, 0);
  if (ret) {
    return ErrUnableToRegisterCQNotifications;
  }

  ret = createQueuePairs(c);
  if (ret) {
    return ret;
  }

  debug("++CLIENT RESOURCES++ ID:(%p) PD(%p) CQ(%p|%d) CC(%p) QP(%p)\n",
        c->cm_event_id, c->PD, c->CQ, c->CQ->cqe, c->completionChannel,
        c->cm_event_id->qp);

  return ret;
}

static int registerServerMetadataBuffer(connection *c) {
  int ret = -1;

  c->metaMR = rdma_buffer_register(c->PD, &c->metaAttr, sizeof(c->metaAttr), (IBV_ACCESS_LOCAL_WRITE));
  if (!c->metaMR) {
    debug("++CLIENT_MR(error)\n");
    return -ENOMEM;
  }

  struct ibv_sge SGE;
  struct ibv_recv_wr RCV_WR;
  struct ibv_recv_wr *BAD_RCV_WR;

  SGE.addr = (uint64_t)c->metaMR->addr;
  SGE.length = c->metaMR->length;
  SGE.lkey = c->metaMR->lkey;

  bzero(&RCV_WR, sizeof(RCV_WR));
  RCV_WR.sg_list = &SGE;
  RCV_WR.num_sge = 1;

  ret = ibv_post_recv(c->cm_event_id->qp, &RCV_WR, &BAD_RCV_WR);
  if (ret) {
    debug("++IBV_POST_REC(ERR), errno: %d \n", ret);
    return ret;
  }
  debug("++IBV_POST_REC \n");

  return NULL;
}

static int send_server_metadata_to_client(connection *c) {
  struct ibv_wc wc;
  int ret = -1;

  c->dataBuffer = calloc(50, 1);
  c->serverMR =
      rdma_buffer_register(c->PD, c->dataBuffer, 50,
                           (IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ |
                            IBV_ACCESS_REMOTE_WRITE));

  if (!c->serverMR) {
    debug("Server failed to create a buffer \n");
    return -ENOMEM;
  }

  c->Server_B2.address = (uint64_t)c->serverMR->addr;
  c->Server_B2.length = (uint32_t)c->serverMR->length;
  c->Server_B2.stag.local_stag = (uint32_t)c->serverMR->lkey;

  c->serverMetaMR = rdma_buffer_register(
      c->PD, &c->Server_B2, sizeof(c->Server_B2), IBV_ACCESS_LOCAL_WRITE);

  if (!c->serverMetaMR) {
    debug("Server failed to create to hold server metadata \n");
    return -ENOMEM;
  }

  c->Server_SGE.addr = (uint64_t)&c->Server_B2;
  c->Server_SGE.length = sizeof(c->Server_B2);
  c->Server_SGE.lkey = c->serverMetaMR->lkey;

  bzero(&c->Server_RCV_WR, sizeof(c->Server_RCV_WR));
  c->Server_RCV_WR.sg_list = &c->Server_SGE;
  c->Server_RCV_WR.num_sge = 1;
  c->Server_RCV_WR.opcode = IBV_WR_SEND;
  c->Server_RCV_WR.send_flags = IBV_SEND_SIGNALED;

  // GENERAATES A WORK COMPLETION EVENT
  ret = ibv_post_send(c->cm_event_id->qp, &c->Server_RCV_WR,
                      &c->Server_BAD_RCV_WR);
  if (ret) {
    debug("Posting of server metdata failed, errno: %d \n", -errno);
    return -errno;
  }

  debug("Local buffer metadata has been sent to the client \n");
  return 0;
}
void *handle_client(void *arg) {
  // void *handle_client(client *c) {
  printf("inside thread\n");
  int ret = -1;
  connection *c = (connection *)arg;
  printf("client: id: %p \n", c->cm_event_id);

  printf("WAITING FOR CLIENT...\n");
  struct ibv_wc wc;
  ret = process_work_completion_events(c->completionChannel, &wc);
  if (ret != 1) {
    debug("Failed to receive , ret = %d \n", ret);
    // return NULL;
  }

  ret = send_server_metadata_to_client(c);
  if (ret) {
    debug("Failed to send server metadata to the client, ret = %d \n", ret);
    return NULL;
  }
  printf("WAITING FOR CLIENT.. 2222.\n");
  ret = process_work_completion_events(c->completionChannel, &wc);
  if (ret != 1) {
    debug("Failed to receive , ret = %d \n", ret);
    // return NULL;
  }

  while (1) {
    printf("?? : %u bytes \n", c->metaAttr.length);
    printf("CLIENT %p\n", &c);
    show_rdma_buffer_attr(&c->metaAttr);
    show_rdma_buffer_attr(&c->Server_B2);

    // printf("%d\n ", (int)c->dataBuffer[0]);
    // printf("%d\n ", (int)c->dataBuffer[1]);
    printf("data: %s\n", c->dataBuffer);
    // struct ibv_wc wc;
    // ret = process_work_completion_events(c->completionChannel, &wc);
    // if (ret != 1) {
    //   debug("Failed to receive , ret = %d \n", ret);
    //   // return NULL;
    // }
    sleep(1);
  };

  return NULL;
}

void *removeClient(void *arg) {
  sleep(2);
  printf("inside thread\n");
  connection *c = (connection *)arg;
  if (!c) {
    debug("client not found during disconnect: %p\n", c);
    return NULL;
  }
  // printf("POST ACK: %p\n", c->cm_event_id);
  //
  // THIS DEADLOCKS ... CAN NOT USE IT
  // ret = rdma_destroy_id(c->cm_event_id);
  // if (ret) {
  //   printf("removed\n");
  //   // debug("Failed to destroy client id cleanly, %d \n", -errno);
  // }

  // printf("removing client last: %p \n", c->cm_event_id);

  rdma_destroy_qp(c->cm_event_id);
  printf("removing client 3\n");

  int ret = -1;
  ret = ibv_destroy_cq(c->CQ);
  if (ret) {
    debug("Failed to destroy completion queue cleanly, %d \n", -errno);
  }

  printf("removing client 5\n");
  ret = ibv_destroy_comp_channel(c->completionChannel);
  if (ret) {
    debug("Failed to destroy completion channel cleanly, %d \n", -errno);
  }
  printf("removing client 6\n");

  rdma_buffer_deregister(c->metaMR);
  printf("removing client 7\n");
  rdma_buffer_free(c->serverMR);
  printf("removing client 8\n");
  rdma_buffer_deregister(c->serverMetaMR);
  printf("removing client 9\n");

  ret = ibv_dealloc_pd(c->PD);
  if (ret) {
    debug("Failed to destroy client protection domain cleanly, %d \n", -errno);
  }

  printf("--CLIENT: %p\n", c->cm_event_id);

  return NULL;
}

static int disconnectClient(struct rdma_cm_event *event) {
  printf("removing client %p\n", event->id);

  int ret = -1;
  pthread_t thread;
  int i;
  for (i = 0; i < MaxConnections; i++) {
    if (connections[i] == 0) {
      continue;
    }

    printf("compare: %p %p\n", connections[i]->cm_event_id, event->id);
    if (connections[i]->cm_event_id == event->id) {
      if (pthread_create(&thread, NULL, removeClient, connections[i]) != 0) {
        // return ErrUnableToCreateThread;
      }
      break;
    }
  }

  ret = rdma_ack_cm_event(event);
  if (ret) {
    printf("DISCONNECT ACK FAILED %d", ret);
    return ret;
  }
  return 0;
}

static int connectionEstablished(struct rdma_cm_event *event) {

  pthread_t thread;
  int i;
  for (i = 0; i < MaxConnections; i++) {
    if (connections[i] != 0) {
      printf("COMPARE: %p == %p\n", connections[i]->cm_event_id, event->id);
      if (connections[i]->cm_event_id == event->id) {
        printf("FOUND IT POINTER!\n");
        if (pthread_create(&thread, NULL, handle_client, connections[i]) != 0) {
          perror("++THREAD(failed)\n");
          return ErrUnableToCreateThread;
        }
        return CodeOK;
      }
    }
  }

  int ret = -1;
  ret = rdma_ack_cm_event(event);
  if (ret) {
    return ret;
  }

  return ErrUnableToEstablishConnection;
}

static int initializeConnectionRequest(struct rdma_cm_event *event) {

  int ret = -1;
  int found = 0;
  int i;
  for (i = 0; i < MaxConnections; i++) {
    if (connections[i] == 0) {
      // printf("PLACING CLIENT IN INDEX %d \n", i);
      connections[i] = malloc(sizeof(connection));
      connections[i]->index = i;
      connections[i]->cm_event_id = event->id;
      found = 1;
      break;
    }
  }
  if (!found) {
    debug("no connection slots available, %d\n", found);
    return ErrUnableToTooManyConnections;
  }

  ret = setup_client_resources(connections[i]);
  if (ret) {
    debug("++RESOURCES, ret = %d \n", ret);
    return ret;
  }

  // the initial handshake is a metadata exchange ??
  ret = registerServerMetadataBuffer(connections[i]);
  if (ret) {
    return ret;
  }

  memset(&connections[i]->conn_param, 0, sizeof(connections[i]->conn_param));

  connections[i]->conn_param.initiator_depth = 3;
  connections[i]->conn_param.responder_resources = 3;
  printf("CALLING ACCEPT \n");
  ret = rdma_ack_cm_event(event);
  if (ret) {
    return ret;
  }

  ret = rdma_accept(connections[i]->cm_event_id, &connections[i]->conn_param);
  if (ret) {
    return ErrUnableToAcceptConnection;
    // debug("++ACCEPT(error), errno: %d \n", -errno);
  }

  return CodeOK;
}

int main(int argc, char **argv) {
  int ret, option;

  struct sockaddr_in server_sockaddr;
  bzero(&server_sockaddr, sizeof server_sockaddr);
  server_sockaddr.sin_family = AF_INET;
  server_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  while ((option = getopt(argc, argv, "a:p:")) != -1) {
    switch (option) {
    case 'a':
      ret = get_addr(optarg, (struct sockaddr *)&server_sockaddr);
      if (ret) {
        debug("Invalid IP \n");
        return ret;
      }
      break;
    case 'p':
      server_sockaddr.sin_port = htons(strtol(optarg, NULL, 0));
      break;
    default:
      usage();
      break;
    }
  }
  if (!server_sockaddr.sin_port) {
    server_sockaddr.sin_port = htons(DEFAULT_RDMA_PORT);
  }

  EventChannel = rdma_create_event_channel();
  if (!EventChannel) {
    // debug("Creating cm event channel failed with errno : (%d)", -errno);
    return ErrUnableToCreateEventChannel;
  }

  ret = rdma_create_id(EventChannel, &serverID, NULL, RDMA_PS_TCP);
  if (ret) {
    // debug("Creating server cm id failed with errno: %d ", -errno);
    return ErrUnableToCreateCMID;
  }
  ret = rdma_bind_addr(serverID, (struct sockaddr *)&server_sockaddr);
  if (ret) {
    // debug("Failed to bind server address, errno: %d \n", -errno);
    return ErrUnableToBindToAddress;
  }

  ret = rdma_listen(serverID, ListenerBacklog);
  if (ret) {
    // debug("rdma_listen failed errno: %d ", -errno);
    return ErrUnableToListenOnAddress;
  }

  // printf("EC: %p\n", &EventChannel);
  // printf("SID: %p\n", &serverID);

  struct rdma_cm_event *newEvent;
  while (1) {
    printf("WAITING FOR EVENT...\n");
    ret = get_rdma_cm_event(EventChannel, &newEvent);
    if (ret) {
      debug("GET event errno: %d \n", -errno);
      continue;
    }

    if (!newEvent) {
      printf("NO EVENT\n");
      continue;
    }

    switch (newEvent->event) {

    case RDMA_CM_EVENT_ADDR_RESOLVED:
    case RDMA_CM_EVENT_ADDR_ERROR:
    case RDMA_CM_EVENT_ROUTE_RESOLVED:
    case RDMA_CM_EVENT_ROUTE_ERROR:
    case RDMA_CM_EVENT_CONNECT_REQUEST:
      initializeConnectionRequest(newEvent);
      break;
    case RDMA_CM_EVENT_CONNECT_RESPONSE:
    case RDMA_CM_EVENT_CONNECT_ERROR:
    case RDMA_CM_EVENT_UNREACHABLE:
    case RDMA_CM_EVENT_REJECTED:
    case RDMA_CM_EVENT_ESTABLISHED:
      connectionEstablished(newEvent);
      break;
    case RDMA_CM_EVENT_DISCONNECTED:
      disconnectClient(newEvent);
      break;
    case RDMA_CM_EVENT_DEVICE_REMOVAL:
    case RDMA_CM_EVENT_MULTICAST_JOIN:
    case RDMA_CM_EVENT_MULTICAST_ERROR:
    case RDMA_CM_EVENT_ADDR_CHANGE:
    case RDMA_CM_EVENT_TIMEWAIT_EXIT:
    default:
      ret = rdma_ack_cm_event(newEvent);
      if (ret) {
        continue;
      }
    }
  }

  return ErrNone;
}

// printf("START\n");
// int ret = startRDMAServer("15.15.15.2", "11111");
// printf("EXIT: %d\n", ret);
// return 0;
// }
