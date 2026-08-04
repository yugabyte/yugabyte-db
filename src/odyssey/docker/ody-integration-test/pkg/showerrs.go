package main

import (
	"context"
	"database/sql"
	"fmt"
	"syscall"
	"time"

	"github.com/jmoiron/sqlx"
)

func getErrs(ctx context.Context, db *sqlx.DB) (map[string]int, error) {
	qry := `SHOW ERRORS`
	rows, err := db.QueryContext(ctx, qry)
	if err != nil {
		return nil, fmt.Errorf("error while exec query %s: %w", qry, err)
	}

	mp := make(map[string]int)

	parser := func(rows *sql.Rows) error {
		for rows.Next() {
			var name string
			var cnt int

			err := rows.Scan(&name, &cnt)
			if err != nil {
				return err
			}

			mp[name] = cnt
		}
		return rows.Close()
	}

	if err := parser(rows); err != nil {
		return nil, fmt.Errorf("error while parsing %w", err)
	}

	return mp, nil
}

func showErrors(ctx context.Context) error {
	// restarting odyssey drops show errs view, but we have change to request show errors to old instance
	// so we explicitly kill old od
	if _, err := signalToProc(syscall.SIGINT, "odyssey"); err != nil {
		return err
	}

	if err := ensureOdysseyRunning(ctx); err != nil {
		return err
	}
	console := "console"
	pgConString := fmt.Sprintf("host=%s port=%d dbname=%s sslmode=disable user=%s", hostname, odyPort, console, username)
	db, err := sqlx.Open("postgres", pgConString)
	if err != nil {
		return err
	}

	if mp, err := getErrs(ctx, db); err != nil {
		return err
	} else {
		for _, name := range []string{
			"OD_EATTACH",
			"OD_EATTACH_TOO_MANY_CONNECTIONS",
			"OD_ESERVER_CONNECT",
			"OD_ESERVER_READ",
			"OD_ESERVER_WRITE",
			"OD_ECLIENT_WRITE",
			"OD_ECLIENT_READ",
			"OD_ESYNC_BROKEN",
			"OD_ROUTER_ERROR_REPLICATION",
			"OD_EOOM",
			"OD_ROUTER_ERROR_TIMEDOUT",
			"OD_ROUTER_ERROR_LIMIT_ROUTE",
			"OD_ROUTER_ERROR_LIMIT",
			"OD_ROUTER_ERROR_NOT_FOUND",
			"OD_ROUTER_ERROR",
		} {
			if mp[name] != 0 {
				return fmt.Errorf("error jst atfer restart: %s - %d", name, mp[name])
			}
		}
	}

	return nil
}

func showErrorsAfterPgRestart(ctx context.Context) error {
	// restarting odyssey drops show errs view, but we have change to request show errors to old instance
	// so we explicitly kill old od
	if _, err := signalToProc(syscall.SIGINT, "odyssey"); err != nil {
		return err
	}

	if err := ensureOdysseyRunning(ctx); err != nil {
		return err
	}

	if err := restartPg(ctx); err != nil {
		return err
	}

	console := "console"
	pgConString := fmt.Sprintf("host=%s port=%d dbname=%s sslmode=disable user=%s", hostname, odyPort, console, username)
	db, err := sqlx.Open("postgres", pgConString)
	if err != nil {
		return err
	}
	cor_cnt := 10

	for i := 0; i < cor_cnt; i++ {
		go selectSleepNoWait(ctx, 10)
	}

	time.Sleep(2 * time.Second)

	/* TODO: drop this test or make it work */
	if _, err := getErrs(ctx, db); err != nil {
		return err
	}

	return nil
}

func odyShowErrsTestSet(ctx context.Context) error {
	if err := showErrors(ctx); err != nil {
		err = fmt.Errorf("show errors failed: %w", err)
		fmt.Println(err)
		return err
	}

	if err := showErrorsAfterPgRestart(ctx); err != nil {
		err = fmt.Errorf("show errors failed: %w", err)
		fmt.Println(err)
		return err
	}

	fmt.Println("odyShowErrsTestSet: Ok")

	return nil
}
