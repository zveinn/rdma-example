package main

import (
	"flag"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"strings"
	"time"
)

var (
	filter     string
	root       string
	sleepTimer int
)

// todo: show_gids
func main() {
	flag.StringVar(&root, "root", "", "starting path for listing")
	flag.StringVar(&filter, "filter", "", "filter for namespaces")
	flag.IntVar(&sleepTimer, "sleep", 0, "sleep timer in MS between file reads")
	flag.Parse()

	// pwd, _ := os.Getwd()
	// reader := bufio.NewReader(os.Stdin)
	// fmt.Println("You are about to scan ", pwd, " press any key to continue..")
	// _, _ = reader.ReadString('\n')

	walkDirectory(root)
}

var walk func(path string, d fs.DirEntry, err error) error

func walkDirectory(root string) error {
	walk = func(path string, d fs.DirEntry, err error) error {
		if err != nil {
			return err
		}

		if !d.IsDir() {
			if filter != "" && !strings.Contains(path, filter) {
				return nil
			}

			if sleepTimer != 0 {
				time.Sleep(time.Duration(sleepTimer) * time.Millisecond)
			}

			vb, err := os.ReadFile(path)
			if err != nil {
				fmt.Println("unable to open:", path, err)
				return nil
			}
			fmt.Fprintf(os.Stdout, "%s\n%s", path, vb)

		}

		return nil
	}

	return filepath.WalkDir(root, walk)
}

// var formatting = make(map[string])
