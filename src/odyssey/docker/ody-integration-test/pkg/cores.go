package main

import (
	_ "bufio"
	_ "bytes"
	"context"
	"fmt"
	"io/ioutil"
	_ "os"
	"os/exec"
	"strconv"
	"syscall"
	"time"
)

const benchTimeSec = 10
const timeSleep = 5
const procName = "odyssey"
const signal = syscall.SIGTERM
const testCount = 100

func bunchProcess(ctx context.Context) {
	_, err := exec.CommandContext(ctx, "pgbench",
		"--builtin", "select-only",
		"-c", "40",
		"-T", strconv.Itoa(benchTimeSec),
		"-j", "20",
		"-n",
		"-h", "localhost",
		"-p", "6432",
		"-U", "postgres",
		"postgres",
		"-P", "1").Output()

	if err != nil {
		fmt.Printf("pgbench error: %v\n", err)
	}
}

func SigTermAfterHighLoad(ctx context.Context) error {
	for i := 0; i < testCount; i++ {
		fmt.Printf("Test number: %d\n", i+1)

		if err := ensurePostgresqlRunning(ctx); err != nil {
			return err
		}

		if err := ensureOdysseyRunning(ctx); err != nil {
			return err
		}

		go bunchProcess(ctx)

		time.Sleep(timeSleep * time.Second)

		if _, err := signalToProc(signal, procName); err != nil {
			fmt.Println(err.Error())
		}
	}

	files, err := ioutil.ReadDir("/var/cores")
	if err != nil {
		fmt.Println(err)
	}
	countCores := len(files)
	coresPercent := (float64(countCores) / float64(testCount)) * 100
	fmt.Printf("Cores count: %d out of %d (%.2f %%)\n", countCores, testCount, coresPercent)

	return nil
}

func odyCoresTestSet(ctx context.Context) error {
	if err := SigTermAfterHighLoad(ctx); err != nil {
		err = fmt.Errorf("odyCoresTestSet failed: %w", err)
		fmt.Println(err)
		return err
	}

	fmt.Println("odyCoresTestSet: Ok")

	return nil
}
