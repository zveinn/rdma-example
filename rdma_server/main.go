package main

/*
#include <stdlib.h>
int *get_error();
*/

import "C"

import (
	"fmt"
	"unsafe"
)

type Error struct {
	MsgLen uint8
	RCode  uint8
	Msg    unsafe.Pointer
}

func (e *Error) String() string {
	return unsafe.String((*byte)(unsafe.Pointer(e.Msg)), e.MsgLen)
}

func main() {
	fmt.Println("meow!")
}
