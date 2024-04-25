
#include "rdma_common.h"
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

static struct rdma_event_channel *EventChannel = NULL;
static struct rdma_cm_id *ServerID = NULL;

typedef struct {
  struct rdma_cm_event *cm_event;
  struct rdma_cm_id *cm_event_id;
  struct ibv_comp_channel *completionChannel;
  struct ibv_pd *PD;
  struct ibv_cq *CQ;
  struct ibv_qp_init_attr QP;

  struct ibv_mr *MR;
  // struct ibv_mr *SERVER_MR;
  // struct ibv_mr *SERVER_BUFFER_MR;
  struct ibv_sge SGE;
  struct ibv_recv_wr RCV_WR;
  struct ibv_recv_wr *BAD_RCV_WR;
  // struct ibv_recv_wr RCV_WR;

  struct rdma_buffer_attr B1;
  struct rdma_buffer_attr B2;
  struct rdma_buffer_attr B3;
  struct rdma_buffer_attr B4;
  int index;
} client;

client *clients[1000];

// ???????
static struct rdma_cm_id *ClientSocket = NULL;
static struct ibv_pd *PD = NULL;
static struct ibv_comp_channel *io_completion_channel = NULL;
static struct ibv_cq *cq = NULL;
static struct ibv_qp_init_attr qp_init_attr;
static struct ibv_qp *client_qp = NULL;

/* RDMA memory resources */
static struct ibv_mr *client_metadata_mr = NULL, *server_buffer_mr = NULL,
                     *server_metadata_mr = NULL;
static struct rdma_buffer_attr client_metadata_attr, server_metadata_attr;
static struct ibv_recv_wr client_recv_wr, *bad_client_recv_wr = NULL;
static struct ibv_send_wr server_send_wr, *bad_server_send_wr = NULL;
static struct ibv_sge client_recv_sge, server_send_sge;

static int setup_client_resources(client *c) {
  int ret = -1;

  c->PD = ibv_alloc_pd(c->cm_event_id->verbs);
  if (!c->PD) {
    rdma_error("Failed to allocate a protection domain errno: %d\n", -errno);
    return -errno;
  }
  debug("++DP %p \n", c->PD);

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
    return -errno;
  }

  debug("++QP %p\n", c->cm_event_id->qp);
  return ret;
}

static int accept_client_connection(client *c) {
  struct rdma_conn_param conn_param;
  struct sockaddr_in remote_sockaddr;
  int ret = -1;

  c->MR = rdma_buffer_register(c->PD, &c->B1, sizeof(c->B1),
                               (IBV_ACCESS_LOCAL_WRITE));
  if (!c->MR) {
    rdma_error("++CLIENT_MR(error)\n");
    return -ENOMEM;
  }

  c->SGE.addr = (uint64_t)c->MR->addr;
  c->SGE.length = c->MR->length;
  c->SGE.lkey = c->MR->lkey;

  bzero(&c->RCV_WR, sizeof(c->RCV_WR));
  c->RCV_WR.sg_list = &c->SGE;
  c->RCV_WR.num_sge = 1;

  ret = ibv_post_recv(c->cm_event_id->qp, &c->RCV_WR, &c->BAD_RCV_WR);
  if (ret) {
    rdma_error("++IBV_POST_REC(ERR), errno: %d \n", ret);
    return ret;
  }
  debug("++IBV_POST_REC \n");

  memset(&conn_param, 0, sizeof(conn_param));

  conn_param.initiator_depth = 3;
  conn_param.responder_resources = 3;
  ret = rdma_accept(c->cm_event_id, &conn_param);
  if (ret) {
    rdma_error("++ACCEPT(error), errno: %d \n", -errno);
    return -errno;
  }

  debug("++ESTABLISHED(waiting...) \n");
  struct rdma_cm_event *cm_event = NULL;
  ret =
      process_rdma_cm_event(EventChannel, RDMA_CM_EVENT_ESTABLISHED, &cm_event);
  if (ret) {
    rdma_error("++ESTABLISHED(error), errnp: %d \n", -errno);
    return -errno;
  }
  debug("++ESTABLISHED(received!) \n");

  ret = rdma_ack_cm_event(cm_event);
  if (ret) {
    rdma_error("++ESTABLISHED(failed to ack) %d\n", -errno);
    return -errno;
  }

  memcpy(&remote_sockaddr, rdma_get_peer_addr(c->cm_event_id),
         sizeof(struct sockaddr_in));
  printf("A new connection is accepted from %s \n",
         inet_ntoa(remote_sockaddr.sin_addr));

  printf("CLIENT: %p \n", c);
  printf("CompCHan %p \n", c->completionChannel);
  printf("PD %p \n", c->PD);
  printf("CQ %p \n", c->CQ);
  printf("QP %p \n", &c->QP);
  printf("MR %p \n", c->MR);
  printf("SGE %p \n", &c->SGE);
  printf("RCV_WR %p \n", &c->RCV_WR);
  printf("BAD-- %p \n", c->BAD_RCV_WR);
  printf("B1 %p \n", &c->B1);
  printf("B2 %p \n", &c->B2);
  printf("B3 %p \n", &c->B3);
  printf("B4 %p \n", &c->B4);

  return ret;
}

static int postConnectReceive(client *c) {
  struct ibv_wc wc;
  int ret = -1;
  ret = process_work_completion_events(c->completionChannel, &wc, 1);
  if (ret != 1) {
    rdma_error("Failed to receive , ret = %d \n", ret);
    return ret;
  }

  printf("RECEIVED WORK!\n");
  printf("Client side buffer information is received...\n");
  show_rdma_buffer_attr(&c->B1);
  return 1;
}

/* This function sends server side buffer metadata to the connected client */
static int send_server_metadata_to_client() {
  struct ibv_wc wc;
  int ret = -1;
  ret = process_work_completion_events(io_completion_channel, &wc, 1);
  if (ret != 1) {
    rdma_error("Failed to receive , ret = %d \n", ret);
    return ret;
  }
  /* if all good, then we should have client's buffer information, lets see */
  printf("Client side buffer information is received...\n");
  show_rdma_buffer_attr(&client_metadata_attr);
  printf("The client has requested buffer length of : %u bytes \n",
         client_metadata_attr.length);
  /* We need to setup requested memory buffer. This is where the client will
   * do RDMA READs and WRITEs. */
  server_buffer_mr =
      rdma_buffer_alloc(PD /* which protection domain */,
                        client_metadata_attr.length /* what size to allocate */,
                        (IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ |
                         IBV_ACCESS_REMOTE_WRITE) /* access permissions */);
  if (!server_buffer_mr) {
    rdma_error("Server failed to create a buffer \n");
    /* we assume that it is due to out of memory error */
    return -ENOMEM;
  }
  /* This buffer is used to transmit information about the above
   * buffer to the client. So this contains the metadata about the server
   * buffer. Hence this is called metadata buffer. Since this is already
   * on allocated, we just register it.
   * We need to prepare a send I/O operation that will tell the
   * client the address of the server buffer.
   */
  server_metadata_attr.address = (uint64_t)server_buffer_mr->addr;
  server_metadata_attr.length = (uint32_t)server_buffer_mr->length;
  server_metadata_attr.stag.local_stag = (uint32_t)server_buffer_mr->lkey;
  server_metadata_mr = rdma_buffer_register(
      PD /* which protection domain*/,
      &server_metadata_attr /* which memory to register */,
      sizeof(server_metadata_attr) /* what is the size of memory */,
      IBV_ACCESS_LOCAL_WRITE /* what access permission */);
  if (!server_metadata_mr) {
    rdma_error("Server failed to create to hold server metadata \n");
    /* we assume that this is due to out of memory error */
    return -ENOMEM;
  }
  /* We need to transmit this buffer. So we create a send request.
   * A send request consists of multiple SGE elements. In our case, we only
   * have one
   */
  server_send_sge.addr = (uint64_t)&server_metadata_attr;
  server_send_sge.length = sizeof(server_metadata_attr);
  server_send_sge.lkey = server_metadata_mr->lkey;
  /* now we link this sge to the send request */
  bzero(&server_send_wr, sizeof(server_send_wr));
  server_send_wr.sg_list = &server_send_sge;
  server_send_wr.num_sge = 1;          // only 1 SGE element in the array
  server_send_wr.opcode = IBV_WR_SEND; // This is a send request
  server_send_wr.send_flags = IBV_SEND_SIGNALED; // We want to get notification
  /* This is a fast data path operation. Posting an I/O request */
  ret = ibv_post_send(
      client_qp /* which QP */,
      &server_send_wr /* Send request that we prepared before */, &bad_server_send_wr /* In case of error, this will contain failed requests */);
  if (ret) {
    rdma_error("Posting of server metdata failed, errno: %d \n", -errno);
    return -errno;
  }
  /* We check for completion notification */
  ret = process_work_completion_events(io_completion_channel, &wc, 1);
  if (ret != 1) {
    rdma_error("Failed to send server metadata, ret = %d \n", ret);
    return ret;
  }
  debug("Local buffer metadata has been sent to the client \n");
  return 0;
}

/* This is server side logic. Server passively waits for the client to call
 * rdma_disconnect() and then it will clean up its resources */
static int disconnect_and_cleanup() {
  struct rdma_cm_event *cm_event = NULL;
  int ret = -1;
  /* Now we wait for the client to send us disconnect event */
  debug("Waiting for cm event: RDMA_CM_EVENT_DISCONNECTED\n");
  ret = process_rdma_cm_event(EventChannel, RDMA_CM_EVENT_DISCONNECTED,
                              &cm_event);
  if (ret) {
    rdma_error("Failed to get disconnect event, ret = %d \n", ret);
    return ret;
  }
  /* We acknowledge the event */
  ret = rdma_ack_cm_event(cm_event);
  if (ret) {
    rdma_error("Failed to acknowledge the cm event %d\n", -errno);
    return -errno;
  }
  printf("A disconnect event is received from the client...\n");
  /* We free all the resources */
  /* Destroy QP */
  rdma_destroy_qp(ClientSocket);
  /* Destroy client cm id */
  ret = rdma_destroy_id(ClientSocket);
  if (ret) {
    rdma_error("Failed to destroy client id cleanly, %d \n", -errno);
    // we continue anyways;
  }
  /* Destroy CQ */
  ret = ibv_destroy_cq(cq);
  if (ret) {
    rdma_error("Failed to destroy completion queue cleanly, %d \n", -errno);
    // we continue anyways;
  }
  /* Destroy completion channel */
  ret = ibv_destroy_comp_channel(io_completion_channel);
  if (ret) {
    rdma_error("Failed to destroy completion channel cleanly, %d \n", -errno);
    // we continue anyways;
  }
  /* Destroy memory buffers */
  rdma_buffer_free(server_buffer_mr);
  rdma_buffer_deregister(server_metadata_mr);
  rdma_buffer_deregister(client_metadata_mr);
  /* Destroy protection domain */
  ret = ibv_dealloc_pd(PD);
  if (ret) {
    rdma_error("Failed to destroy client protection domain cleanly, %d \n",
               -errno);
    // we continue anyways;
  }
  /* Destroy rdma server id */
  // ret = rdma_destroy_id(server_socket);
  // if (ret) {
  //   rdma_error("Failed to destroy server id cleanly, %d \n", -errno);
  //   // we continue anyways;
  // }
  // rdma_destroy_event_channel(cm_event_channel);
  printf("Server shut-down is complete \n");
  return 0;
}

void *handle_client(void *arg) {
  // void *handle_client(client *c) {
  printf("inside thread");
  int ret;
  client *c = (client *)arg;
  ClientSocket = c->cm_event_id;
  printf("client: %p -- event: %p -- id: %p \n", c, c->cm_event,
         c->cm_event->id);

  ret = setup_client_resources(c);
  if (ret) {
    rdma_error("Failed to setup client resources, ret = %d \n", ret);
    return NULL;
  }
  ret = accept_client_connection(c);
  if (ret) {
    rdma_error("Failed to handle client cleanly, ret = %d \n", ret);
    return NULL;
  }
  ret = postConnectReceive(c);
  if (ret) {
    rdma_error("failed at last step, ret = %d \n", ret);
    return NULL;
  }
  // ret = send_server_metadata_to_client();
  // if (ret) {
  //   rdma_error("Failed to send server metadata to the client, ret = %d \n",
  //              ret);
  //   return NULL;
  // }
  // ret = disconnect_and_cleanup();
  // if (ret) {
  //   rdma_error("Failed to clean up resources properly, ret = %d \n", ret);
  //   return NULL;
  // }

  return NULL;
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

  ret = rdma_listen(ServerID, 8);
  if (ret) {
    rdma_error("rdma_listen failed errno: %d ", -errno);
    return -errno;
  }

  printf("LISTEN @ %s , port: %d \n", inet_ntoa(server_addr->sin_addr),
         ntohs(server_addr->sin_port));

  while (1) {
    pthread_t thread;

    int i;
    for (i = 0; i < 1000; i++) {
      if (clients[i] == 0) {
        clients[i] = malloc(sizeof(client));
        clients[i]->index = i;

        debug("WAITING ON CLIENT %d \n", 1);
        ret = process_rdma_cm_event(EventChannel, RDMA_CM_EVENT_CONNECT_REQUEST,
                                    &clients[i]->cm_event);

        clients[i]->cm_event_id = clients[i]->cm_event->id;
        if (ret) {
          rdma_error("Failed to get cm event, ret = %d \n", ret);
          return ret;
        }

        ret = rdma_ack_cm_event(clients[i]->cm_event);
        if (ret) {
          rdma_error("Failed to acknowledge the cm event errno: %d \n", -errno);
          return -errno;
        }

        if (pthread_create(&thread, NULL, handle_client, clients[i]) != 0) {
          perror("++THREAD(failed)\n");
          exit(EXIT_FAILURE);
        }
        break;
      }
    }
  }

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
