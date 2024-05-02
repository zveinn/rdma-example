package main

import (
	"fmt"
	"time"
)

type (
	ERROR_CODE string
	ERROR_ID   int
)

var (
	ErrNotFound ERROR_CODE = "This was not found"
	ErrInternal ERROR_CODE = "This was not found"
)

const (
	M1 ERROR_ID = iota
	M2
	M3
)

func main() {
	x := time.Now()
	fmt.Println(x.Format(time.RFC3339))
}

func FourOFour() ERROR_ID {
	return M1
}
