package main

import (
	"context"
	"github.com/jmoiron/sqlx"
	"log"
	"sync"

	"fmt"
	_ "github.com/lib/pq"
	"time"
)

var dbname = "db1"

func prep() {
	ctx := context.TODO()

	var err error
	db, err := sqlx.ConnectContext(ctx, "postgres", fmt.Sprintf("host=localhost port=6432 user=user1 dbname=%s sslmode=disable", dbname))
	if err != nil {
		log.Fatal(err)
	}

	for j := 0; j < 10; j++ {

		var wg sync.WaitGroup
		wg.Add(10)

		stmt, err := db.Prepare(fmt.Sprintf("INSERT INTO sh1.foo%d VALUES($1)", j))
		if err != nil {
			log.Fatal(err)
		}

		for i := 0; i < 10; i++ {
			go func(wg *sync.WaitGroup) {
				defer wg.Done()

				_, err = stmt.Exec(i)
				if err != nil {
					log.Fatal(err)
				}
			}(&wg)
		}

		wg.Add(10)
		stmt2, err := db.Prepare("SELECT pg_sleep($1)")
		if err != nil {
			log.Fatal(err)
		}

		for i := 0; i < 10; i++ {
			go func(wg *sync.WaitGroup) {
				defer wg.Done()

				_, err = stmt2.Exec(1)
				if err != nil {
					log.Fatal(err)
				}
			}(&wg)
		}

		wg.Wait()

		err = stmt.Close()
		if err != nil {
			log.Fatal(err)
		}

		err = stmt2.Close()
		if err != nil {
			log.Fatal(err)
		}
	}
	db.Close()
	log.Println("OK")
}

func main() {

	for j := 0; j < 10; j++ {
		for i := 0; i < 10; i++ {
			go prep()
		}

		log.Println("ITER DONE")
		time.Sleep(10 * time.Second)
	}
	log.Println("TEST OK")
}
