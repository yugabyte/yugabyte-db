package main

import (
	"fmt"

	_ "github.com/lib/pq"
)

// var dsn string = "host={host} port={port} dbname={dbname} user={username} password={password} sslmode=disable"
var dsn string = "host=127.0.0.1 port=5432 dbname=postgres user=postgres password=agens sslmode=disable"

// var graphName string = "{graph_path}"
var graphName string = "testGraph"

func main() {

	// Do cypher query to AGE with database/sql Tx API transaction conrol
	fmt.Println("# Do cypher query with SQL API")
	doWithSqlAPI(dsn, graphName)

	// Do cypher query to AGE with Age API
	fmt.Println("# Do cypher query with Age API")
	doWithAgeWrapper(dsn, graphName)
}
