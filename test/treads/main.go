/*
#Cgo amd64 CFLAGS=-I. LDFLAGS=-L. -lc_array
*/
package main

/*
#include <pthread.h>
#include <stdlib.h>


pthread_t pthread_self(void);
void ProcessSlice(int length, void *data);
int *get_data(int size);
int *get_error();
void free_data(int *data);
*/
import "C"

import (
	"fmt"
	"reflect"
	"runtime"
	"time"
	"unsafe"
)

var (
	x  = []byte{5, 5, 5, 1}
	x2 = []byte{5, 5, 5, 1}
	x3 = []byte{5, 5, 5, 1}
)

func main() {
	tid := C.pthread_self()
	fmt.Println("Current thread ID (Linux specific):", tid)
	// Create a Go slice

	// Call GoSliceToC to pass data to C
	// go GoSliceToC()
	// go modifySlice()
	GetArrayFromC()
	GetError()

	// for {
	// 	time.Sleep(1 * time.Second)
	// }
}

func modifySlice() {
	count := 0
	for {
		count++
		x[0] = byte(count)
		// if count%2 == 0 {
		// 	*&x[0] = *&x2[0]
		// } else {
		// 	*&x[0] = *&x3[0]
		// }
		x = append(x, x2...)
		// x2 = append(x2, x...)
		// *&x[0] = *&x2[0]
		// fmt.Printf("%p %p %p\n", &x[0], &x2[0], &x3[0])
		fmt.Println(x[0])
		time.Sleep(1000 * time.Millisecond)
		runtime.GC()
	}
}

//export GoSliceToC
func GoSliceToC() {
	// C.ProcessSlice(C.int(len(x)), unsafe.Pointer(&x[0]))
	// go changeOriginal(&x[0])
	C.ProcessSlice(C.int(len(x)), unsafe.Pointer(&x[0]))
}

func changeOriginal(x *byte) {
	count := 9
	for {
		count++
		dd := byte(count)
		*x = *&dd
		time.Sleep(1 * time.Second)
	}
	return
}

type Error struct {
	MsgLen uint8
	RCode  uint8
	Msg    unsafe.Pointer
}

func (e *Error) String() string {
	return unsafe.String((*byte)(unsafe.Pointer(e.Msg)), e.MsgLen)
}

func GetError() {
	cArr := C.get_error()
	// fmt.Println(*cArr.Msg)
	// goMSG := (*[]uint8)(unsafe.Pointer(cArr.Msg))
	goErr := (*Error)(unsafe.Pointer(cArr))
	fmt.Println(goErr)

	// goMSG := unsafe.Slice((*byte)(unsafe.Pointer(goErr.Msg)), goErr.MsgLen)
	// fmt.Println(goMSG, string(goMSG))
	// goArr := (*byte)(unsafe.Pointer(cArr)), size)
	// x := new(Error)
	// fmt.Println(goErr)
	// fmt.Println(goErr.Msg)
	// x := C.CString(goErr.Msg)
}

func GetArrayFromC() {
	size := 10
	// Call the C function to get the pre-allocated array
	cArr := C.get_data(C.int(size))

	// Convert the C pointer to a Go slice using unsafe

	// fmt.Println(reflect.TypeOf(cArr))
	// goArr := make([]byte, 10, 10)
	goArr := unsafe.Slice((*byte)(unsafe.Pointer(cArr)), size)
	fmt.Println(goArr)
	fmt.Println(reflect.TypeOf(goArr))
	fmt.Printf("%p %p\n", &goArr[0], &goArr)
	goArr = append(goArr, []byte{10, 10, 10, 10, 10}...)
	fmt.Printf("%p %p\n", &goArr[0], &goArr)

	// Print the received array elements
	fmt.Println("Array from C:")
	for _, val := range goArr {
		fmt.Println(val)
	}

	// Call the C function to free the allocated memory
	C.free_data(cArr)
}
