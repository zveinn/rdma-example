package main

// #cgo LDFLAGS: -lrdmacm
// #include <rdma/rdma_cma.h>
import "C"

import (
	"fmt"
	"reflect"
)

func main() {
	RdmaCreateEventChannel()
}

func RdmaCreateEventChannel() {
	channel := C.rdma_create_event_channel()
	fmt.Println("CHAN:", channel)
	fmt.Printf("CHAN: %p\n", channel)
	fmt.Printf("CHAN: %p\n", &channel)
	fmt.Println(reflect.TypeOf(channel))
	// fmt.Println(channel.(*main._Ctype_struct_rdma_event_channel))

	///
	// x := new(C.rdma_cm_id)
	C.rdma_create_id(channel, nil, nil, 0x0106)
}
