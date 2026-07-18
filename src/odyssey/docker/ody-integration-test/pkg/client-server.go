package main

import (
	"context"
	"fmt"
	"math/rand"
	"sync"
	"syscall"
	"time"
)

func usrReadResultWhilesigusr2Test(
	ctx context.Context,
) error {

	err := ensurePostgresqlRunning(ctx)
	if err != nil {
		return err
	}

	if err := ensureOdysseyRunning(ctx); err != nil {
		return err
	}

	db, err := getConn(ctx, databaseName, 1)
	if err != nil {
		return err
	}
	t, err := db.Beginx()
	if err != nil {
		return err
	}

	ch := make(chan error, 1)

	go func(chan error) {
		_, err := t.Queryx("select pg_sleep(100)")
		ch <- err
	}(ch)

	if _, err := signalToProc(syscall.SIGUSR2, "odyssey"); err != nil {
		return err
	}

	err, ok := <-ch
	close(ch)

	if err != nil || !ok {
		return fmt.Errorf("connection closed or reset\n")
	}

	return nil
}

func select42(ctx context.Context, ch chan error, wg *sync.WaitGroup) {
	defer wg.Done()

	db, err := getConn(ctx, databaseName, 1)
	if err != nil {
		ch <- err
		fmt.Println(err)
		return
	}

	if _, err := db.Query("Select 42"); err != nil {
		ch <- err
		fmt.Println(err)
		return
	}

	fmt.Printf("select 42 OK\n")
}

func selectSleepNoWait(ctx context.Context, i int) error {
	db, err := getConn(ctx, databaseName, 1)
	if err != nil {
		return err
	}
	defer db.Close()

	qry := fmt.Sprintf("SELECT pg_sleep(%d)", i)

	fmt.Printf("selectSleep: doing query %s\n", qry)

	_ = db.QueryRowContext(ctx, qry)
	fmt.Print("select sleep OK\n")
	return err
}

func selectSleep(ctx context.Context, i int, ch chan error, wg *sync.WaitGroup, before_restart bool) {
	defer wg.Done()
	db, err := getConn(ctx, databaseName, 1)
	if err != nil {

		if before_restart {
			err = fmt.Errorf("before restart coroutine: %w", err)
		} else {
			err = fmt.Errorf("after restart coroutine: %w", err)
		}

		ch <- err
		fmt.Println(err)
		return
	}

	defer db.Close()

	qry := fmt.Sprintf("SELECT pg_sleep(%d)", i)

	fmt.Printf("selectSleep: doing query %s\n", qry)

	r, err := db.QueryContext(ctx, qry)
	if err != nil {
		if before_restart {
			err = fmt.Errorf("before restart coroutine failed %w", err)
		} else {
			err = fmt.Errorf("after restart coroutine failed %w", err)
		}
		fmt.Printf("sleep coroutine fail time: %s\n", time.Now().Format(time.RFC3339Nano))
		ch <- err
		return
	} else {
		for r.Next() {
			r.Scan(struct {
			}{})
		}
		r.Close()
	}
	ch <- nil
	fmt.Print("select sleep OK\n")
}

const (
	sleepInterval      = 10
	maxCoroutineFailOk = 4
)

func waitOnChan(ch chan error, maxOK int) error {
	failedCnt := 0
	for {
		select {
		case err, ok := <-ch:
			if !ok {
				if failedCnt > maxOK {
					return fmt.Errorf("too many coroutines failed")
				}
				return nil
			}
			if err != nil {
				fmt.Println(err)
				failedCnt++
			}
		}
	}
}

func onlineRestartTest(ctx context.Context) error {
	err := ensurePostgresqlRunning(ctx)
	if err != nil {
		return err
	}

	if err := ensureOdysseyRunning(ctx); err != nil {
		return err
	}

	coroutineSleepCnt := 5
	repeatCnt := 4

	for j := 0; j < repeatCnt; j++ {
		fmt.Printf("Iter %d\n", j)
		ch := make(chan error, coroutineSleepCnt*5)
		wg := sync.WaitGroup{}
		{
			for i := 0; i < coroutineSleepCnt; i++ {
				wg.Add(1)
				go selectSleep(ctx, sleepInterval, ch, &wg, true)
			}

			for i := 0; i < coroutineSleepCnt; i++ {
				wg.Add(1)
				go func() {
					time.Sleep(time.Millisecond * time.Duration(rand.Intn(1000)))
					select42(ctx, ch, &wg)
				}()
			}

			// to make sure previous select was in old ody
			time.Sleep(1 * time.Second)

			restartOdyssey(ctx)

			for i := 0; i < coroutineSleepCnt*2; i++ {
				wg.Add(1)
				go selectSleep(ctx, sleepInterval, ch, &wg, false)
			}

			for i := 0; i < coroutineSleepCnt; i++ {
				wg.Add(1)
				go func() {
					time.Sleep(time.Millisecond * time.Duration(rand.Intn(1000)))
					select42(ctx, ch, &wg)
				}()
			}
		}
		wg.Wait()
		fmt.Println("onlineRestartTest: wait done, closing channel")
		close(ch)
		// no single coroutine should fail!
		if err := waitOnChan(ch, 7); err != nil {
			fmt.Println(fmt.Errorf("online restart failed %w", err))
			return err
		}
		time.Sleep(1 * time.Second)
		fmt.Println("Iter complete")
	}
	if _, err := signalToProc(syscall.SIGINT, "odyssey"); err != nil {
		return err
	}
	return nil
}

func sigusr2Test(
	ctx context.Context,
) error {

	err := ensurePostgresqlRunning(ctx)
	if err != nil {
		return err
	}

	if err := ensureOdysseyRunning(ctx); err != nil {
		return err
	}

	coroutineSleepCnt := 10

	ch := make(chan error, coroutineSleepCnt+1)
	wg := sync.WaitGroup{}
	{
		for i := 0; i < coroutineSleepCnt; i++ {
			wg.Add(1)
			go selectSleep(ctx, sleepInterval, ch, &wg, true)
		}

		time.Sleep(1 * time.Second)
		wg.Add(1)
		go sigusr2Odyssey(ctx, ch, &wg)

	}

	wg.Wait()
	fmt.Println("sigusr2Test: wait done, closing channel")
	close(ch)
	if err := waitOnChan(ch, 7); err != nil {
		fmt.Println(fmt.Errorf("sigusr2 failed %w", err))
		return err
	}

	if _, err := signalToProc(syscall.SIGINT, "odyssey"); err != nil {
		return err
	}
	return nil
}

func odyClientServerInteractionsTestSet(ctx context.Context) error {


	if err := usrReadResultWhilesigusr2Test(ctx); err != nil {
		err = fmt.Errorf("usrReadResultWhilesigusr2: %w", err)
		fmt.Println(err)
		return err
	}

	if err := onlineRestartTest(ctx); err != nil {
		err = fmt.Errorf("online restart error %w", err)
		fmt.Println(err)
		return err
	}

	if err := sigusr2Test(ctx); err != nil {
		err = fmt.Errorf("sigusr2 error %w", err)
		fmt.Println(err)
		return err
	}

	fmt.Println("odyClientServerInteractionsTestSet: Ok")

	return nil
}
