package main

import (
	"context"
	"fmt"
	"syscall"
	"time"
)

func SigintAfterSigusr2Test(ctx context.Context) error {

	if err := ensureOdysseyRunning(ctx); err != nil {
		return err
	}

	go selectSleepNoWait(ctx, 10000)

	time.Sleep(1 * time.Second)

	if _, err := signalToProc(syscall.SIGUSR2, "odyssey"); err != nil {
		return err
	}
	if _, err := pidNyName("odyssey"); err != nil {
		return err
	}
	if _, err := signalToProc(syscall.SIGINT, "odyssey"); err != nil {
		return err
	}

	time.Sleep(1 * time.Second)

	if err := OdysseyIsAlive(ctx); err == nil {
		return fmt.Errorf("odyssey ignores sigint")
	}
	return nil
}

func odySignalsTestSet(ctx context.Context) error {
	if err := SigintAfterSigusr2Test(ctx); err != nil {
		err = fmt.Errorf("signals test failed: %w", err)
		fmt.Println(err)
		return err
	}

	fmt.Println("odySignalsTestSet: Ok")

	return nil
}
