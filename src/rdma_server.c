
#include "rdma_common.h"
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

static struct rdma_event_channel *EventChannel = NULL;
static struct rdma_cm_id *ServerID = NULL;

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
} client;

client *clients[10000];
client *requested_clients[10000];

static int disconnectServer() {
  int ret = -1;
  ret = rdma_destroy_id(ServerID);
  if (ret) {
    rdma_error("Failed to destroy server id cleanly, %d \n", -errno);
  }
  rdma_destroy_event_channel(EventChannel);
  printf("Server shut-down is complete \n");
  return ret;
}

static int setup_client_resources(client *c) {
  int ret = -1;

  c->PD = ibv_alloc_pd(c->cm_event_id->verbs);
  if (!c->PD) {
    rdma_error("++PD errno: %d\n", -errno);
    return -errno;
  }
  debug("++PD %p \n", c->PD);

  c->completionChannel = ibv_create_comp_channel(c->cm_event_id->verbs);
  if (!c->completionChannel) {
    rdma_error("++COMPCHANNEL(error), %d\n", -errno);
    return -errno;
  }
  debug("++COMPCHANNEL %p \n", c->completionChannel);

  c->CQ = ibv_create_cq(c->cm_event_id->verbs, CQ_CAPACITY, NULL,
                        c->completionChannel, 0);
  if (!c->CQ) {
    rdma_error("++CQ(error), errno: %d\n", -errno);
    return -errno;
  }
  debug("++CQ %p with %d elements \n", c->CQ, c->CQ->cqe);

  ret = ibv_req_notify_cq(c->CQ, 0);
  if (ret) {
    rdma_error("++NOTIFY(error) errno: %d \n", -errno);
    return -errno;
  }

  return ret;
}

static int registerServerMetadataBuffer(client *c) {
  int ret = -1;

  printf("++LOCAL META\n");
  c->metaMR = rdma_buffer_register(c->PD, &c->metaAttr, sizeof(c->metaAttr),
                                   (IBV_ACCESS_LOCAL_WRITE));
  if (!c->metaMR) {
    rdma_error("++CLIENT_MR(error)\n");
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
    rdma_error("++IBV_POST_REC(ERR), errno: %d \n", ret);
    return ret;
  }
  debug("++IBV_POST_REC \n");

  // struct ibv_wc wc;
  // ret = process_work_completion_events(c->completionChannel, &wc, 1);
  // if (ret != 1) {
  //   rdma_error("Failed to receive , ret = %d \n", ret);
  //   return ret;
  // }

  return NULL;
}

static int register_meta(client *c) {
  struct rdma_conn_param conn_param;
  struct sockaddr_in remote_sockaddr;
  int ret = -1;

  ret = registerServerMetadataBuffer(c);
  if (ret) {
    return ret;
  }

  // c->metaMR = rdma_buffer_register(c->PD, &c->metaAttr, sizeof(c->metaAttr),
  //                                  (IBV_ACCESS_LOCAL_WRITE));
  // if (!c->metaMR) {
  //   rdma_error("++CLIENT_MR(error)\n");
  //   return -ENOMEM;
  // }
  //
  // c->SGE.addr = (uint64_t)c->metaMR->addr;
  // c->SGE.length = c->metaMR->length;
  // c->SGE.lkey = c->metaMR->lkey;
  //
  // bzero(&c->RCV_WR, sizeof(c->RCV_WR));
  // c->RCV_WR.sg_list = &c->SGE;
  // c->RCV_WR.num_sge = 1;
  //
  // ret = ibv_post_recv(c->cm_event_id->qp, &c->RCV_WR, &c->BAD_RCV_WR);
  // if (ret) {
  //   rdma_error("++IBV_POST_REC(ERR), errno: %d \n", ret);
  //   return ret;
  // }
  // debug("++IBV_POST_REC \n");

  // memset(&conn_param, 0, sizeof(conn_param));
  //
  // conn_param.initiator_depth = 3;
  // conn_param.responder_resources = 3;
  // ret = rdma_accept(c->cm_event_id, &conn_param);
  // if (ret) {
  //   rdma_error("++ACCEPT(error), errno: %d \n", -errno);
  //   return -errno;
  // }
  //
  // debug("++ESTABLISHED(waiting...) \n");
  // struct rdma_cm_event *cm_event = NULL;
  // ret =
  //     process_rdma_cm_event(EventChannel, RDMA_CM_EVENT_ESTABLISHED,
  //     &cm_event);
  // if (ret) {
  //   rdma_error("++ESTABLISHED(error), errnp: %d \n", -errno);
  //   return -errno;
  // }
  // debug("++ESTABLISHED(received!) \n");
  //
  // ret = rdma_ack_cm_event(cm_event);
  // if (ret) {
  //   rdma_error("++ESTABLISHED(failed to ack) %d\n", -errno);
  //   return -errno;
  // }

  // memcpy(&remote_sockaddr, rdma_get_peer_addr(c->cm_event_id),
  //        sizeof(struct sockaddr_in));
  // printf("A new connection is accepted from %s \n",
  //        inet_ntoa(remote_sockaddr.sin_addr));

  printf("CLIENT: %p \n", c);
  printf("CompCHan %p \n", c->completionChannel);
  printf("PD %p \n", c->PD);
  printf("CQ %p \n", c->CQ);
  printf("QP %p \n", &c->QP);
  printf("MR %p \n", c->metaMR);
  printf("SGE %p \n", &c->SGE);
  printf("RCV_WR %p \n", &c->RCV_WR);
  printf("BAD-- %p \n", c->BAD_RCV_WR);
  printf("B1 %p \n", &c->metaAttr);
  printf("B2 %p \n", &c->Server_B2);
  printf("B3 %p \n", &c->B3);
  printf("B4 %p \n", &c->B4);

  return ret;
}

// char *convert_to_string(uint32_t *data, uint32_t length) {
//   char *str = malloc(length * sizeof(char));
//   if (str == NULL) {
//     return NULL;
//   }
//   printf("length: %d\n", length);
//   for (int i = 0; i < length; i++) {
//     printf("> %d\n", data[i]);
//   }
//   return str;
// }
//

static int createQueuePairs(client *c) {
  int ret;
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
    rdma_error("++QP(error) errno: %d\n", -errno);
    return ret;
  }

  debug("++QP %p\n", c->cm_event_id->qp);
}

static int send_server_metadata_to_client(client *c) {
  struct ibv_wc wc;
  int ret = -1;
  ret = process_work_completion_events(c->completionChannel, &wc, 1);
  if (ret != 1) {
    rdma_error("Failed to receive , ret = %d \n", ret);
    return ret;
  }

  sleep(1);
  printf("???...\n");
  show_rdma_buffer_attr(&c->metaAttr);
  printf("?? : %u bytes \n", c->metaAttr.length);

  c->dataBuffer = calloc(4, 1);
  c->serverMR =
      rdma_buffer_register(c->PD, c->dataBuffer, 4,
                           (IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ |
                            IBV_ACCESS_REMOTE_WRITE));

  if (!c->serverMR) {
    rdma_error("Server failed to create a buffer \n");
    return -ENOMEM;
  }

  c->Server_B2.address = (uint64_t)c->serverMR->addr;
  c->Server_B2.length = (uint32_t)c->serverMR->length;
  c->Server_B2.stag.local_stag = (uint32_t)c->serverMR->lkey;

  c->serverMetaMR = rdma_buffer_register(
      c->PD, &c->Server_B2, sizeof(c->Server_B2), IBV_ACCESS_LOCAL_WRITE);

  if (!c->serverMetaMR) {
    rdma_error("Server failed to create to hold server metadata \n");
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

  ret = ibv_post_send(c->cm_event_id->qp, &c->Server_RCV_WR,
                      &c->Server_BAD_RCV_WR);
  if (ret) {
    rdma_error("Posting of server metdata failed, errno: %d \n", -errno);
    return -errno;
  }

  ret = process_work_completion_events(c->completionChannel, &wc, 1);
  if (ret != 1) {
    rdma_error("Failed to send server metadata, ret = %d \n", ret);
    return ret;
  }
  debug("Local buffer metadata has been sent to the client \n");
  return 0;
}

/* This is server side logic. Server passively waits for the client to call
 * rdma_disconnect() and then it will clean up its resources */
static int disconnect_and_cleanup(client *c) {
  struct rdma_cm_event *cm_event = NULL;
  int ret = -1;

  debug("Waiting for cm event: RDMA_CM_EVENT_DISCONNECTED\n");
  ret = process_rdma_cm_event(EventChannel, RDMA_CM_EVENT_DISCONNECTED,
                              &cm_event);
  if (ret) {
    rdma_error("Failed to get disconnect event, ret = %d \n", ret);
    return ret;
  }

  ret = rdma_ack_cm_event(cm_event);
  if (ret) {
    rdma_error("Failed to acknowledge the cm event %d\n", -errno);
    return -errno;
  }
  printf("A disconnect event is received from the client...\n");
  rdma_destroy_qp(c->cm_event_id);
  ret = rdma_destroy_id(c->cm_event_id);
  if (ret) {
    rdma_error("Failed to destroy client id cleanly, %d \n", -errno);
  }
  ret = ibv_destroy_cq(c->CQ);
  if (ret) {
    rdma_error("Failed to destroy completion queue cleanly, %d \n", -errno);
  }

  ret = ibv_destroy_comp_channel(c->completionChannel);
  if (ret) {
    rdma_error("Failed to destroy completion channel cleanly, %d \n", -errno);
  }

  rdma_buffer_deregister(c->metaMR);
  rdma_buffer_free(c->serverMR);
  rdma_buffer_deregister(c->serverMetaMR);

  ret = ibv_dealloc_pd(c->PD);
  if (ret) {
    rdma_error("Failed to destroy client protection domain cleanly, %d \n",
               -errno);
  }

  return 0;
}

void *handle_client(void *arg) {
  // void *handle_client(client *c) {
  printf("inside thread\n");
  int ret = -1;
  client *c = (client *)arg;
  printf("client: id: %p \n", c->cm_event_id);

  ret = createQueuePairs(c);
  if (ret) {
    return NULL;
  }

  // ret = setup_client_resources(c);
  // if (ret) {
  //   rdma_error("Failed to setup client resources, ret = %d \n", ret);
  //   return NULL;
  // }

  ret = register_meta(c);
  if (ret) {
    return NULL;
  }

  ret = send_server_metadata_to_client(c);
  if (ret) {
    rdma_error("Failed to send server metadata to the client, ret = %d \n",
               ret);
    return NULL;
  }
  // ret = disconnect_and_cleanup(c);
  // if (ret) {
  //   rdma_error("Failed to clean up resources properly, ret = %d \n", ret);
  //   return NULL;
  // }
  //
  while (1) {
    sleep(1);
  };

  return NULL;
}

static void initializeConnectionRequest(struct rdma_cm_event *event) {
  int ret = -1;

  int i;
  for (i = 0; i < 10000; i++) {
    if (requested_clients[i] == 0) {
      printf("PLACING CLIENT IN INDEX %d \n", i);
      requested_clients[i] = malloc(sizeof(client));
      requested_clients[i]->index = i;
      requested_clients[i]->cm_event_id = event->id;
      break;
    }
  }

  ret = setup_client_resources(requested_clients[i]);
  if (ret) {
    rdma_error("++RESOURCES, ret = %d \n", ret);
    return;
  }

  //
  // ret = register_meta(requested_clients[i]);
  // if (ret) {
  //   rdma_error("Failed to handle client cleanly, ret = %d \n", ret);
  //   return;
  // }

  // struct sockaddr_in remote_sockaddr;
  memset(&requested_clients[i]->conn_param, 0,
         sizeof(requested_clients[i]->conn_param));

  requested_clients[i]->conn_param.initiator_depth = 3;
  requested_clients[i]->conn_param.responder_resources = 3;
  printf("CALLING ACCEPT \n");
  ret = rdma_accept(requested_clients[i]->cm_event_id,
                    &requested_clients[i]->conn_param);
  if (ret) {
    rdma_error("++ACCEPT(error), errno: %d \n", -errno);
  }
}
static void connectionEstablished(struct rdma_cm_event *event) {
  pthread_t thread;
  int i;
  for (i = 0; i < 10000; i++) {
    if (requested_clients[i] != 0) {
      printf("COMPARE: %p == %p\n", requested_clients[i]->cm_event_id,
             event->id);

      if (requested_clients[i]->cm_event_id == event->id) {
        printf("FOUND IT POINTER!\n");
        if (pthread_create(&thread, NULL, handle_client,
                           requested_clients[i]) != 0) {
          perror("++THREAD(failed)\n");
          exit(EXIT_FAILURE);
        }
        printf("DONE\n");
        break;
      }
      // if (requested_clients[i]->cm_event->param.conn.qp_num ==
      //     event->param.conn.qp_num) {
      //   printf("FOUND IT!\n");
      // }
    }
  }

  // for (i = 0; i < 10000; i++) {
  //   if (clients[i] == 0) {
  //     clients[i] = malloc(sizeof(client));
  //     clients[i]->index = i;
  //     clients[i]->cm_event_id = event->id;
  //     break;
  //   }
  // }
}

/* Starts an RDMA server by allocating basic connection resources */
static int start_rdma_server(struct sockaddr_in *server_addr) {
  int ret = -1;
  EventChannel = rdma_create_event_channel();
  if (!EventChannel) {
    rdma_error("Creating cm event channel failed with errno : (%d)", -errno);
    return -errno;
  }
  debug("+EventChannel: %p\n", EventChannel);

  ret = rdma_create_id(EventChannel, &ServerID, NULL, RDMA_PS_TCP);
  if (ret) {
    rdma_error("Creating server cm id failed with errno: %d ", -errno);
    return -errno;
  }
  debug("+ServerID: %p\n", ServerID);

  ret = rdma_bind_addr(ServerID, (struct sockaddr *)server_addr);
  if (ret) {
    rdma_error("Failed to bind server address, errno: %d \n", -errno);
    return -errno;
  }
  debug("+BIND to ServerID: %p\n", ServerID);

  ret = rdma_listen(ServerID, 20);
  if (ret) {
    rdma_error("rdma_listen failed errno: %d ", -errno);
    return -errno;
  }

  printf("LISTEN @ %s , port: %d \n", inet_ntoa(server_addr->sin_addr),
         ntohs(server_addr->sin_port));

  struct rdma_cm_event *event;
  while (1) {
    ret = get_rdma_cm_event(EventChannel, &event);
    if (ret) {
      rdma_error("GET event errno: %d \n", -errno);
      continue;
    }

    switch (event->event) {
    case RDMA_CM_EVENT_CONNECT_REQUEST:
      initializeConnectionRequest(event);
      break;
    case RDMA_CM_EVENT_ESTABLISHED:
      connectionEstablished(event);
      printf("...sleep\n");
      // sleep(2);
      break;
    case RDMA_CM_EVENT_DISCONNECTED:
      // acceptConnection(event);
      break;
    case RDMA_CM_EVENT_REJECTED:
      // acceptConnection(event);
      printf("REJECT: %s\n", rdma_event_str(event->event));
      break;
    default:
      printf("Unknown event: %s\n", rdma_event_str(event->event));
    }
    ret = rdma_ack_cm_event(event);
    if (ret) {
      rdma_error("ACK event errno: %d \n", -errno);
      continue;
    }
  }
  //
  // while (1) {
  //   pthread_t thread;
  //
  //   int i;
  //   for (i = 0; i < 1000; i++) {
  //     if (clients[i] == 0) {
  //       clients[i] = malloc(sizeof(client));
  //       clients[i]->index = i;
  //
  //       // debug("WAITING ON CLIENT %d \n", 1);
  //       // ret = process_rdma_cm_event(EventChannel,
  //       // RDMA_CM_EVENT_CONNECT_REQUEST, );
  //       ret = rdma_ack_cm_event(clients[i]->cm_event);
  //       if (ret) {
  //         rdma_error("Failed to acknowledge the cm "
  //                    "event errno: %d \n",
  //                    -errno);
  //         return -errno;
  //       }
  //
  //       clients[i]->cm_event_id = clients[i]->cm_event->id;
  //       if (ret) {
  //         rdma_error("Failed to get cm event, ret = %d \n", ret);
  //         return ret;
  //       }
  //
  //       if (pthread_create(&thread, NULL, handle_client, clients[i]) != 0) {
  //         perror("++THREAD(failed)\n");
  //         exit(EXIT_FAILURE);
  //       }
  //       break;
  //     }
  //   }
  // }

  return ret;
}

void usage() {
  printf("Usage:\n");
  printf("rdma_server: [-a <server_addr>] [-p <server_port>]\n");
  printf("(default port is %d)\n", DEFAULT_RDMA_PORT);
  exit(1);
}

int main(int argc, char **argv) {
  int ret, option;
  struct sockaddr_in server_sockaddr;
  bzero(&server_sockaddr, sizeof server_sockaddr);
  server_sockaddr.sin_family = AF_INET; /* standard IP NET address */
  server_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY); /* passed address */
  /* Parse Command Line Arguments, not the most reliable code */
  while ((option = getopt(argc, argv, "a:p:")) != -1) {
    switch (option) {
    case 'a':
      /* Remember, this will overwrite the port info */
      ret = get_addr(optarg, (struct sockaddr *)&server_sockaddr);
      if (ret) {
        rdma_error("Invalid IP \n");
        return ret;
      }
      break;
    case 'p':
      /* passed port to listen on */
      server_sockaddr.sin_port = htons(strtol(optarg, NULL, 0));
      break;
    default:
      usage();
      break;
    }
  }
  if (!server_sockaddr.sin_port) {
    /* If still zero, that mean no port info provided */
    server_sockaddr.sin_port = htons(DEFAULT_RDMA_PORT); /* use default port */
  }

  ret = start_rdma_server(&server_sockaddr);
  if (ret) {
    rdma_error("RDMA server failed to start cleanly, ret = %d \n", ret);
    return ret;
  }
  return 0;
}
