/*
 * Header file for the common RDMA routines used in the server/client example
 * program.
 *
 * Author: Animesh Trivedi
 *          atrivedi@apache.org
 *
 */

#include <errno.h>
#include <getopt.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/poll.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
/* NEW STUFF */
/* NEW STUFF */
/* NEW STUFF */
static int ListenerBacklog = 20;

static int16_t ErrNone = 0;
static int16_t ErrInvalidServerIP = 1;
static int16_t ErrUnableToCreateEventChannel = 2;
static int16_t ErrUnableToCreateServerCMID = 3;
static int16_t ErrUnableToBindToAddress = 4;
static int16_t ErrUnableToListenOnAddress = 5;
static int16_t ErrUnableToResolveAddress = 6;
static int16_t ErrUnableToTooManyConnections = 7;
static int16_t ErrUnableToEstablishConnection = 8;
static int16_t ErrUnableToAcceptConnection = 9;
static int16_t ErrUnableToAllocatePD = 10;
static int16_t ErrUnableToCreateCompletionChannel = 11;
static int16_t ErrUnableToCreateCompletionQueue = 12;
static int16_t ErrUnableToRegisterCQNotifications = 13;
static int16_t ErrUnableToCreateQueuePairs = 14;
static int16_t ErrUnableToGetFromEventChannel = 15;
static int16_t ErrUnableToPollEventChannelFD = 16;

static int16_t ErrUnexpectedEventStatus = 77;
static int16_t ErrUnexpectedEventType = 78;

static int16_t ErrUnableToGetEventChannelFlags = 96;
static int16_t ErrUnableToSetEventChannelToNoneBlocking = 97;
static int16_t ErrUnableToAckEvent = 98;
static int16_t ErrUnableToCreateThread = 99;

static int16_t CodeOK = 100;

uint32_t makeError(int8_t funcCode, int8_t minioCode, int8_t extra1, int8_t extra2);
uint64_t timestampDiff(struct timespec *t1, struct timespec *t2);

#ifndef RDMA_COMMON_H
#define RDMA_COMMON_H

// #include <infiniband/verbs.h>
// #include <rdma/rdma_cma.h>
// #include "../infiniband/verbs.h"
// #include "../rdma/rdma_cma.h"

#define LOCAL_HEADER (0)
// #define SERVER_HEADER (1)
#ifdef LOCAL_HEADER
#include "../infiniband/verbs.h"
#include "../rdma/rdma_cma.h"
#endif

#ifdef SERVER_HEADER
#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>
#endif

/* Error Macro*/
#define debug(msg, args...) \
  fprintf(stdout, "%s: %d|| " msg, __FILE__, __LINE__, ##args);

/* Debug Macro */
#define ACN_RDMA_DEBUG (1)

/* Capacity of the completion queue (CQ) */
#define CQ_CAPACITY (200)
/* MAX SGE capacity */
#define MAX_SGE (10)
/* MAX work requests */
#define MAX_WR (10)
/* Default port where the RDMA server is listening */
#define DEFAULT_RDMA_PORT (22222)

uint32_t pollEventChannel(
    struct rdma_event_channel *channel,
    enum rdma_cm_event_type type,
    int expectedStatus,
    int timeout,
    struct rdma_cm_event **event);
/*
 * We use attribute so that compiler does not step in and try to pad the
 * structure. We use this structure to exchange information between the server
 * and the client.
 *
 * For details see: http://gcc.gnu.org/onlinedocs/gcc/Type-Attributes.html
 */
struct __attribute((packed)) rdma_buffer_attr {
  uint64_t address;
  uint32_t length;
  union stag {
    /* if we send, we call it local stags */
    uint32_t local_stag;
    /* if we receive, we call it remote stag */
    uint32_t remote_stag;
  } stag;
};
/* resolves a given destination name to sin_addr */
int get_addr(char *dst, struct sockaddr *addr);

/* prints RDMA buffer info structure */
void show_rdma_buffer_attr(struct rdma_buffer_attr *attr);

/*
 * Processes an RDMA connection management (CM) event.
 * @echannel: CM event channel where the event is expected.
 * @expected_event: Expected event type
 * @cm_event: where the event will be stored
 */
int process_rdma_cm_event(struct rdma_event_channel *echannel,
                          enum rdma_cm_event_type expected_event,
                          struct rdma_cm_event **cm_event);

/*
 * Get a RDMA connection management (CM) event.
 * @echannel: CM event channel where the event is expected.
 * @cm_event: where the event will be stored
 */
int get_rdma_cm_event(struct rdma_event_channel *echannel,
                      struct rdma_cm_event **cm_event);

/* Allocates an RDMA buffer of size 'length' with permission permission. This
 * function will also register the memory and returns a memory region (MR)
 * identifier or NULL on error.
 * @pd: Protection domain where the buffer should be allocated
 * @length: Length of the buffer
 * @permission: OR of IBV_ACCESS_* permissions as defined for the enum
 * ibv_access_flags
 */
struct ibv_mr *rdma_buffer_alloc(struct ibv_pd *pd, uint32_t length,
                                 enum ibv_access_flags permission);

/* Frees a previously allocated RDMA buffer. The buffer must be allocated by
 * calling rdma_buffer_alloc();
 * @mr: RDMA memory region to free
 */
void rdma_buffer_free(struct ibv_mr *mr);

/* This function registers a previously allocated memory. Returns a memory
 * region (MR) identifier or NULL on error.
 * @pd: protection domain where to register memory
 * @addr: Buffer address
 * @length: Length of the buffer
 * @permission: OR of IBV_ACCESS_* permissions as defined for the enum
 * ibv_access_flags
 */
struct ibv_mr *rdma_buffer_register(struct ibv_pd *pd, void *addr,
                                    uint32_t length,
                                    enum ibv_access_flags permission);
/* Deregisters a previously register memory
 * @mr: Memory region to deregister
 */
void rdma_buffer_deregister(struct ibv_mr *mr);

/* Processes a work completion (WC) notification.
 * @comp_channel: Completion channel where the notifications are expected to
 * arrive
 * @wc: Array where to hold the work completion elements
 * @max_wc: Maximum number of expected work completion (WC) elements. wc must be
 *          atleast this size.
 */
int process_work_completion_events(struct ibv_comp_channel *comp_channel,
                                   struct ibv_wc *wc);

/* prints some details from the cm id */
void show_rdma_cmid(struct rdma_cm_id *id);

#endif /* RDMA_COMMON_H */
