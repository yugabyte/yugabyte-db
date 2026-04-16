package main

import (
	"context"
	"fmt"
	"os"

	"github.com/jackc/pgproto3"
	"github.com/jackc/pgx/v4"
)

func syncPackets(ctx context.Context) error {

	err := ensurePostgresqlRunning(ctx)
	if err != nil {
		return err
	}

	err = ensureOdysseyRunning(ctx)
	if err != nil {
		return err
	}

	conn, err := pgx.Connect(ctx, "host=localhost port=6432 user=postgres database=postgres")
	if err != nil {
		_, _ = fmt.Fprintf(os.Stderr, "Unable to connection to database: %v\n", err)
		return err
	}
	pgConn := conn.PgConn().Conn()
	buf := make([]byte, 8192)
	buf = (&pgproto3.Query{String: "select 1"}).Encode(buf)
	buf[0] = 0x80
	buf[1] = 0x80
	buf[2] = 0x80
	buf[3] = 0x80
	_, err = pgConn.Write(buf)
	if err != nil {
		return err
	}

	p := make([]byte, 8192)
	_, err = pgConn.Read(p)

	return OdysseyIsAlive(ctx)
}

func odyPkgSyncTestSet(ctx context.Context) error {
	if err := syncPackets(ctx); err != nil {
		err = fmt.Errorf("package sync error %w", err)
		fmt.Println(err)
		return err
	}

	fmt.Println("odyPkgSyncTestSet: Ok")

	return nil
}
