package main

// #cgo LDFLAGS: -lrdmacm
// #include <rdma/rdma_cma.h>
import "C"

import (
	"fmt"
	"unsafe"
)

func main() {
	RdmaCreateEventChannel()
}

func RdmaCreateEventChannel() {
	channel := C.rdma_create_event_channel()
	fmt.Println("CHAN:", channel)
	fmt.Printf("CHAN: %p", channel)
	fmt.Printf("CHAN: %p", &channel)
	fmt.Printf("CHANU: %p", unsafe.Pointer(uint32(channel)))
}
