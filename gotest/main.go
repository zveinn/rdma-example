package main

// #cgo LDFLAGS: -lrdmacm
// #include <rdma/rdma_cma.h>
import "C"

import (
	"fmt"
)

func main() {
	RdmaCreateEventChannel()
}

func RdmaCreateEventChannel() {
	channel := C.rdma_create_event_channel()
	fmt.Println("CHAN:", channel)
	fmt.Printf("CHAN: %p", channel)
	fmt.Printf("CHAN: %p", &channel)
	fmt.Println(channel.(*_Ctype_struct_rdma_event_channel))
}
