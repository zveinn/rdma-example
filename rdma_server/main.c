#include "rdma_common.h"
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

void usage() {
  printf("Usage:\n");
  printf("rdma_server: [-a <server_addr>] [-p <server_port>]\n");
  printf("(default port is %d)\n", DEFAULT_RDMA_PORT);
  exit(1);
}
const int ListenerBacklog = 2000;

const int ErrNone = 0;
const int ErrInvalidServerIP = 1;
const int ErrUnableToCreateEventChannel = 2;
const int ErrUnableToCreateServerCMID = 3;
const int ErrUnableToBindToAddress = 4;
const int ErrUnableToListenOnAddress = 5;
const int ErrUnableToTooManyConnections = 6;
const int ErrUnableToEstablishConnection = 7;
const int ErrUnableToAcceptConnection = 8;
const int ErrUnableToCreateThread = 99;

const int CodeOK = 100;

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

void *handle_client(void *arg) {
  // void *handle_client(client *c) {
  printf("inside thread\n");
  int ret = -1;
  connection *c = (connection *)arg;
  printf("client: id: %p \n", c->cm_event_id);

  // ret = register_meta(c);
  // if (ret) {
  //   return NULL;
  // }
  struct ibv_wc wc;
  ret = process_work_completion_events(c->completionChannel, &wc, 1);
  if (ret != 1) {
    debug("Failed to receive , ret = %d \n", ret);
    return NULL;
  }

  // ret = send_server_metadata_to_client(c);
  // if (ret) {
  //   debug("Failed to send server metadata to the client, ret = %d \n", ret);
  //   return NULL;
  // }
  //
  // ret = disconnect_and_cleanup(c);
  // if (ret) {
  //   rdma_error("Failed to clean up resources properly, ret = %d \n", ret);
  //   return NULL;
  // }
  //
  while (1) {
    printf("data: %s\n", c->dataBuffer);
    sleep(1);
  };

  return NULL;
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

  return ErrUnableToEstablishConnection;
}

static int initializeConnectionRequest(struct rdma_cm_event *event) {

  int i;
  uint8_t found = 0;
  for (i = 0; i < MaxConnections; i++) {
    if (connections[i] != 0) {
      // printf("PLACING CLIENT IN INDEX %d \n", i);
      connections[i] = malloc(sizeof(connection));
      connections[i]->index = i;
      connections[i]->cm_event_id = event->id;
      found = 1;
      break;
    }
  }
  if (found == 0) {
    return ErrUnableToTooManyConnections;
  }

  // ret = setup_client_resources(connections[i]);
  // if (ret) {
  //   rdma_error("++RESOURCES, ret = %d \n", ret);
  //   return;
  // }
  //
  // //
  // ret = register_meta(connections[i]);
  // if (ret) {
  //   rdma_error("Failed to handle client cleanly, ret = %d \n", ret);
  //   return;
  // }

  // struct sockaddr_in remote_sockaddr;
  memset(&connections[i]->conn_param, 0, sizeof(connections[i]->conn_param));

  connections[i]->conn_param.initiator_depth = 3;
  connections[i]->conn_param.responder_resources = 3;
  printf("CALLING ACCEPT \n");
  int ret =
      rdma_accept(connections[i]->cm_event_id, &connections[i]->conn_param);
  if (ret) {
    return ErrUnableToAcceptConnection;
    // debug("++ACCEPT(error), errno: %d \n", -errno);
  }
}
int startRDMAServer(char *addr, char *port) {
  int ret, option;

  struct sockaddr_in server_sockaddr;

  bzero(&server_sockaddr, sizeof server_sockaddr);
  server_sockaddr.sin_family = AF_INET;
  server_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  server_sockaddr.sin_port = (int)strtol(port, NULL, 0);

  ret = get_addr(addr, (struct sockaddr *)&server_sockaddr);
  if (ret) {
    return ErrInvalidServerIP;
  }

  EventChannel = rdma_create_event_channel();
  if (!EventChannel) {
    // debug("Creating cm event channel failed with errno : (%d)", -errno);
    return ErrUnableToCreateEventChannel;
  }

  ret = rdma_create_id(EventChannel, &serverID, NULL, RDMA_PS_TCP);
  if (ret) {
    // debug("Creating server cm id failed with errno: %d ", -errno);
    return ErrUnableToCreateServerCMID;
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

  struct rdma_cm_event *newEvent;
  while (1) {
    printf("WAIT FOR EVENT\n");
    ret = get_rdma_cm_event(EventChannel, &newEvent);
    if (ret) {
      debug("GET event errno: %d \n", -errno);
      continue;
    }
    if (!newEvent) {
      printf("NO EVENT\n");
      continue;
    }

    printf("++++++EVENT: %d\n", newEvent->event);
    switch (newEvent->event) {

    case RDMA_CM_EVENT_ADDR_RESOLVED:
      break;
    case RDMA_CM_EVENT_ADDR_ERROR:
      break;
    case RDMA_CM_EVENT_ROUTE_RESOLVED:
      break;
    case RDMA_CM_EVENT_ROUTE_ERROR:
      break;
    case RDMA_CM_EVENT_CONNECT_REQUEST:
      initializeConnectionRequest(newEvent);
      break;
    case RDMA_CM_EVENT_CONNECT_RESPONSE:
      break;
    case RDMA_CM_EVENT_CONNECT_ERROR:
      break;
    case RDMA_CM_EVENT_UNREACHABLE:
      break;
    case RDMA_CM_EVENT_REJECTED:
      break;
    case RDMA_CM_EVENT_ESTABLISHED:
      connectionEstablished(newEvent);
      break;
    case RDMA_CM_EVENT_DISCONNECTED:
      break;
    case RDMA_CM_EVENT_DEVICE_REMOVAL:
      break;
    case RDMA_CM_EVENT_MULTICAST_JOIN:
      break;
    case RDMA_CM_EVENT_MULTICAST_ERROR:
      break;
    case RDMA_CM_EVENT_ADDR_CHANGE:
      break;
    case RDMA_CM_EVENT_TIMEWAIT_EXIT:

    default:
      debug("UNKOWN EVENT: %s\n", rdma_event_str(newEvent->event));
    }
    ret = rdma_ack_cm_event(newEvent);
    if (ret) {
      // debug("ACK event errno: %d \n", -errno);
      continue;
    }
  }

  return ErrNone;
}

int main(int argc, char **argv) {
  printf("START\n");
  int ret = startRDMAServer("15.15.15.2", "11111");
  printf("EXIT: %d\n", ret);
  return 0;
}
