























START SERVER...
event = nil
> channel := rdma_create_event_channel
> cm_server_id := ???
> rdma_create_id(channel , &cm_server_id)
	--- cm_server_id is populated inside rdma_create_id
> rdma_bind_address(cm_server_id, SOCKET_ADDRESS)
> rdma_listen(cm_server_id, 8)
  --- 8 client ?? .. tells us to check out 'man listen'

  EVENT := new(rdma_cm_event)
> process_rdma_cm_event(channel, rdma_cm_event_type, &EVENT)
> rdma_get_cm_event(channel, &EVENT)
NOTE: always call rdma_ack_cm_event .. anytime we get an even object.
NOTE: look at rdma_cm_event_types
> cm_client_id = EVENT->id
> rdma_event_str .. returns event as string

-------------------------------------------------
-------------------------------------------------
-------------------------------------------------
-------------------------------------------------

CREATE CLIENT RESOURCE (probably the memory space...)
CONCEPTS: Protection Domains, Memory Buffers, Complation Queues, Queue Pair
> PD := ibv_alloc_pd(cm_client_id->verbs)
	--- protected domain
> CHAN := ibv_create_comp_channel(cm_client_id->verbs)
	-- creates a completion channel
> CQ := ibv_create_cq(cm_client_id->verbs, capacity, CHAN, 0 )
	-- creates a completion queue
  -- 0 == signaling vector
> ibv_req_notify_cq(CQ, 0)
	-- 0 == EVENT TYPE (all events)

> ibv_create_qp(cm_client_id, PM, &qp_init_attr )
	-- SEE BELOW FOR qp_init_attr

> CLIENT_QP = cm_client_id->qp

-------------------------------------------------
-------------------------------------------------
-------------------------------------------------
-------------------------------------------------

ACCEPTING CLIENT CONNECTION

> CLIENT_META_MR := rdma_buffer_register(PD, &rdma_buffer_attr, sizeOf(rdma_buffer_attr), IBV_ACCESS_LOCAL_WRITE)
	--- rdma_buffer_attr is a type.. variable is user_metadata_attr (but it's a stupid name)
	--- last parameter is permissions for PD?
	--- client_metadata_attr == rdma_buffer_attr
	--- PD is found in the second above

> ret = ibv_post_recv(CLIENT_QP, &client_recv_wr, &bad_client_recv_wr)

> rdma_accept()
	--- accepts rdma connection
> process_rdma_event(RDMA_CM_EVENT_ESTABLISHED)
	--- RDMA_CM_EVENT_ESTABLISHED == indicates a success connection state
> rdma_ack_cm_event(cm_event)
> rdma_get_peer_addr(cm_client_id)
  --- this gets your a remote socket address


-------------------------------------------------
-------------------------------------------------
-------------------------------------------------
-------------------------------------------------

SEND BUFFER METADATA TO CLIENT
> process_work_completion_events(io_completion_channel, &wc, 1)
	--- what is 1 ???

> DEBUG: show_rdma_buffer_attr(&rdma_buffer_attr)
	--- rdma_buffer_attr is a type.. variable is user_metadata_attr (but it's a stupid name)

> SERVER_BUFFER := rmda_buffer_alloc(PD, rdma_buffer_attr.length, PERMISSIONS)
  --- IBV > ACCESS_LOCAL_WRITE, ACCESS_REMOTE_READ, ACCESS_REMOTE_WRITE

 NOTE: here we set server meta attributes (addr, length, lkey)

> SERVER_META_MR := rdma_buffer_register(pd, %server_meta_attr, sizeOf(server_meta_attr), IBV_ACCESS_LOCAL_WRITE)

NOTE: here we set information for &server_send_sge

> ibv_post_send(CLIENT_QP, &server_send_wr, &bad_server_send_wr)
	--- bad_server_send_wr ... is a pointer to receive error

> process_work_completion_event(io_completion_channel, &wc, 1)
	--- what is 1 ???

-------------------------------------------------
-------------------------------------------------
-------------------------------------------------
-------------------------------------------------

DISCONNECT AND CLEANUP 
> disconnect_and_cleanup()

-------------------------------------------------
-------------------------------------------------
-------------------------------------------------
-------------------------------------------------

NOTES:
CM == CONNECTION MANAGER
ACK to event will free up the memory



rdma_event_channel is used to push ASYNC events, each channel is a file descriptor
-- call: rdma_get_cm_event to get events
-- calls rdma_destroy_event_channel to close

RDMA_ID: rdma_create_id ( THIS ACTUALLY CREATES THE SOCKET )
RDMA_ID == socket
- context
- ps .. port space



CLIENT .....
1. prep connection
2. rdma_buffer_register .. tell servers to do 1 WORK into this buffer
		-- used for connect response

SERVER
 - rdma_create_event_channel
	- rdma_create_id <- THIS IS THE SOCET
		- rdma_bind_addr
			- rdma_listen - THIS IS NON-BLOCKING

FOR ....
	- rdma_get_cm_event(channel, &event) ( if connect event then CM_CLIENT_ID == event->id)
	- rdma_ack_cm_event(event) -- ALWAYS ACK EVENTS

RDMA RESOURCE CREATION
 - PD = ibv_alloc_pd(cm_client_id->verbs)
 - CHAN = ibv_create_comp_channel(cm_client_id->verbs)
 - CQ = ibv_create_cq(cm_client_id->verbs, CAPACITY, CONTEXT??, CHAN, 0??)
 - ibv_req_notify_cq(CQ, 0 = event_type)
 - rdma_create_qp(CM_CLIENT_ID, PD, &qp_init_attr)


FOR ....
	- rdma_get_cm_event(channel, &event)
	- rdma_ack_cm_event(event) -- ALWAYS ACK EVENTS


-------------------------------------------------
-------------------------------------------------
-------------------------------------------------
-------------------------------------------------
-------------------------------------------------
-------------------------------------------------
-------------------------------------------------
-------------------------------------------------
-------------------------------------------------
-------------------------------------------------
-------------------------------------------------
-------------------------------------------------
FROM GEMENI

server_loop() {
  event_channel = rdma_create_event_channel()
  cm_id = rdma_create_id(event_channel)

  /* Bind address (replace with your desired address) */
  rdma_bind_addr(cm_id, "192.168.1.100", 18515)

  rdma_listen(cm_id, MAX_BACKLOG)

  while (TRUE) {
    event = rdma_get_cm_event(event_channel)

    switch (event->event_type) {
      case RDMA_CM_EVENT_CONNECT_REQUEST:
        new_cm_id = rdma_create_id(event_channel)
        /* Accept the connection request */
        rdma_accept(event->cm_id, new_cm_id)
        /* Process new connection (e.g., create QP) */
        process_connection(new_cm_id)
        break;
      case RDMA_CM_EVENT_ADDR_RESOLVED:
      case RDMA_CM_EVENT_ROUTE_RESOLVED:
      case RDMA_CM_EVENT_CONNECTED:
        /* Handle connection establishment events (optional) */
        break;
      case RDMA_CM_EVENT_DISCONNECTED:
        /* Handle connection disconnect (clean up resources) */
        break;
      case RDMA_CM_EVENT_REJECTED:
        /* Handle connection rejection (optional) */
        break;
      default:
        /* Handle unexpected events */
        break;
    }

    rdma_ack_cm_event(event)
  }

  rdma_destroy_id(cm_id)
  rdma_destroy_event_channel(event_channel)
}

process_connection(cm_id) {
  /* Create Queue Pair (QP) using cm_id */
  qp = create_qp(cm_id)

  /* ... (data transfer and communication logic using the qp) */

  /* Free resources when connection is no longer needed */
  destroy_qp(qp)
  rdma_destroy_id(cm_id)
}
