package main

import (
	"bytes"
	"flag"
	"fmt"
	"log"
	"os/exec"
	"runtime/debug"
	"strings"
	"time"
)

var (
	filter     string
	cmd        string
	cmdParsed  []string
	sleepTimer int
	A          int
	B          int
)

// todo: show_gids
func main() {
	flag.IntVar(&A, "A", 0, "print N lines after filter")
	flag.IntVar(&B, "B", 0, "print N lines before filter")
	flag.StringVar(&cmd, "cmd", "", "command for watch")
	flag.StringVar(&filter, "f", "", "filter for watch")
	flag.IntVar(&sleepTimer, "n", 0, "interval")
	flag.Parse()

	args := flag.Args()
	fmt.Println(args)
	cmdParsed = strings.Split(cmd, " ")
	watch()
}

func watch() {
	defer func() {
		r := recover()
		if r != nil {
			log.Println(r, string(debug.Stack()))
		}
	}()
	for {
		fmt.Println(cmdParsed)
		fmt.Print("\033[H\033[2J")
		var err error
		var out []byte
		if len(cmdParsed) > 1 {
			out, err = exec.Command(cmdParsed[0], cmdParsed[1:]...).CombinedOutput()
		} else {
			out, err = exec.Command(cmdParsed[0]).CombinedOutput()
		}
		// cmd := exec.Command(cmdParsed[0], cmdParsed[1:]...)
		// cmd.Stdout = os.Stdout
		// cmd.Stdin = os.Stdin
		// _ = cmd.Run()
		// fmt.Println("OUT:", out)
		if err != nil {
			fmt.Println(err)
			panic(err)
		}
		splitOut := bytes.Split(out, []byte{10})
		for i, v := range splitOut {
			if filter != "" {
				if bytes.Contains(v, []byte(filter)) {
					// fmt.Println(string(splitOut[i : i+B]))
					if B != 0 {
						for ii := i - 1; ii > i-B-1; ii-- {
							if ii < 0 {
								break
							}
							fmt.Println(string(splitOut[ii]))
						}
					}
					fmt.Println(string(v))
					if A != 0 {
						for ii := i + 1; ii < i+A+1; ii++ {
							if ii >= len(splitOut) {
								break
							}
							fmt.Println(string(splitOut[ii]))
						}
					}
				}
			} else {
				fmt.Println(string(v))
			}
		}

		time.Sleep(time.Millisecond * time.Duration(sleepTimer))
	}
}
