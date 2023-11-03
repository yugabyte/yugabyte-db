package main

import (
	"context"
	"fmt"
	"os"

	_ "github.com/lib/pq"
)

func main() {
	ctx := context.Background()

	for _, f := range []func(ctx2 context.Context) error{
		odyClientServerInteractionsTestSet,
		odyPkgSyncTestSet,
		odyShowErrsTestSet,
		odySignalsTestSet,
	} {
		err := f(ctx)
		if err != nil {
			os.Exit(3)
		}
	}

	fmt.Println("done")
}
