package main

// #include "wrap.h"
import "C"
import "fmt"

func main() {
	C.hello()
	RdmaCreateEventChannel()

	// Use the channel object (cast if necessary)
}

func RdmaCreateEventChannel() {
	channel := C.rdma_create_event_channel_wrapper()
	fmt.Println("CHAN:", channel)
}
