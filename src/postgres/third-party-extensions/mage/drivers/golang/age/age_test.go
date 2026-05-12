/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */
package age

import (
	"fmt"
	"reflect"
	"testing"

	"database/sql"

	_ "github.com/lib/pq"
)

var dsn string = "host=127.0.0.1 port=5432 dbname=postgres user=postgres password=agens sslmode=disable"
var graphName string = "testGraph"

func TestAdditional(t *testing.T) {
	db, err := sql.Open("postgres", dsn)
	if err != nil {
		t.Fatal(err)
	}

	_, err = GetReady(db, graphName)
	if err != nil {
		t.Fatal(err)
	}

	cursor, err := db.Begin()
	if err != nil {
		t.Fatal(err)
	}

	_, err = ExecCypher(cursor, graphName, 0, "CREATE (n:Person {name: '%s', weight:%f})", "Joe", 67.3)
	if err != nil {
		t.Fatal(err)
	}

	_, err = ExecCypher(cursor, graphName, 0, "CREATE (n:Person {name: '%s', weight:77.3, roles:['Dev','marketing']})", "Jack")
	if err != nil {
		t.Fatal(err)
	}

	_, err = ExecCypher(cursor, graphName, 0, "CREATE (n:Person {name: '%s', weight:%d})", "Andy", 59)
	if err != nil {
		t.Fatal(err)
	}
	cursor.Commit()

	cursor, err = db.Begin()
	if err != nil {
		t.Fatal(err)
	}

	cypherCursor, err := ExecCypher(cursor, graphName, 1, "MATCH (n:Person) RETURN n")

	if err != nil {
		t.Fatal(err)
	}

	for cypherCursor.Next() {
		entities, err := cypherCursor.GetRow()
		if err != nil {
			t.Fatal(err)
		}
		vertex := entities[0].(*Vertex)
		fmt.Println(vertex.Id(), vertex.Label(), vertex.Props())
	}

	_, err = ExecCypher(cursor, graphName, 0, "MATCH (a:Person), (b:Person) WHERE a.name='%s' AND b.name='%s' CREATE (a)-[r:workWith {weight: %d}]->(b)",
		"Jack", "Joe", 3)

	if err != nil {
		t.Fatal(err)
	}

	_, err = ExecCypher(cursor, graphName, 0, "MATCH (a:Person {name: '%s'}), (b:Person {name: '%s'}) CREATE (a)-[r:workWith {weight: %d}]->(b)",
		"Joe", "Andy", 7)
	if err != nil {
		t.Fatal(err)
	}

	cursor.Commit()

	cursor, err = db.Begin()
	if err != nil {
		t.Fatal(err)
	}

	cypherCursor, err = ExecCypher(cursor, graphName, 1, "MATCH p=()-[:workWith]-() RETURN p")
	if err != nil {
		t.Fatal(err)
	}

	for cypherCursor.Next() {
		entities, err := cypherCursor.GetRow()
		if err != nil {
			t.Fatal(err)
		}

		path := entities[0].(*Path)

		vertexStart := path.GetAsVertex(0)
		edge := path.GetAsEdge(1)
		vertexEnd := path.GetAsVertex(2)

		fmt.Println(vertexStart, edge, vertexEnd)
	}

	_, err = ExecCypher(cursor, graphName, 0, "MATCH (n:Person) DETACH DELETE n RETURN *")
	if err != nil {
		t.Fatal(err)
	}
	cursor.Commit()
}

func TestAgeWrapper(t *testing.T) {
	ag, err := ConnectAge(graphName, dsn)

	if err != nil {
		t.Fatal(err)
	}

	tx, err := ag.Begin()
	if err != nil {
		t.Fatal(err)
	}

	_, err = tx.ExecCypher(0, "CREATE (n:Person {name: '%s'})", "Joe")
	if err != nil {
		t.Fatal(err)
	}

	_, err = tx.ExecCypher(0, "CREATE (n:Person {name: '%s', age: %d})", "Smith", 10)
	if err != nil {
		t.Fatal(err)
	}

	_, err = tx.ExecCypher(0, "CREATE (n:Person {name: '%s', weight:%f})", "Jack", 70.3)
	if err != nil {
		t.Fatal(err)
	}

	tx.Commit()

	tx, err = ag.Begin()
	if err != nil {
		t.Fatal(err)
	}

	cursor, err := tx.ExecCypher(1, "MATCH (n:Person) RETURN n")
	if err != nil {
		t.Fatal(err)
	}

	count := 0
	for cursor.Next() {
		entities, err := cursor.GetRow()
		if err != nil {
			t.Fatal(err)
		}
		count++
		vertex := entities[0].(*Vertex)
		fmt.Println(count, "]", vertex.Id(), vertex.Label(), vertex.Props())
	}

	fmt.Println("Vertex Count:", count)

	_, err = tx.ExecCypher(0, "MATCH (a:Person), (b:Person) WHERE a.name='%s' AND b.name='%s' CREATE (a)-[r:workWith {weight: %d}]->(b)",
		"Jack", "Joe", 3)
	if err != nil {
		t.Fatal(err)
	}

	_, err = tx.ExecCypher(0, "MATCH (a:Person {name: '%s'}), (b:Person {name: '%s'}) CREATE (a)-[r:workWith {weight: %d}]->(b)",
		"Joe", "Smith", 7)
	if err != nil {
		t.Fatal(err)
	}

	tx.Commit()

	tx, err = ag.Begin()
	if err != nil {
		t.Fatal(err)
	}

	cursor, err = tx.ExecCypher(1, "MATCH p=()-[:workWith]-() RETURN p")
	if err != nil {
		t.Fatal(err)
	}

	count = 0
	for cursor.Next() {
		entities, err := cursor.GetRow()
		if err != nil {
			t.Fatal(err)
		}
		count++
		path := entities[0].(*Path)

		fmt.Println(count, "]", path.GetAsVertex(0), path.GetAsEdge(1).props, path.GetAsVertex(2))
	}

	_, err = tx.ExecCypher(0, "MATCH (n:Person) DETACH DELETE n RETURN *")
	if err != nil {
		t.Fatal(err)
	}
	tx.Commit()
}

func TestQueryWithMapper(t *testing.T) {
	db, err := sql.Open("postgres", dsn)
	if err != nil {
		t.Fatal(err)
	}

	// Confirm graph_path created
	_, err = GetReady(db, graphName)
	if err != nil {
		t.Fatal(err)
	}

	// Tx begin for execute create vertex
	tx, err := db.Begin()
	if err != nil {
		t.Fatal(err)
	}

	// Create Vertex
	ExecCypher(tx, graphName, 0, "CREATE (n:Person {name: '%s'})", "Joe")
	ExecCypher(tx, graphName, 0, "CREATE (n:Person {name: '%s', age: %d})", "Smith", 10)
	ExecCypher(tx, graphName, 0, "CREATE (n:Person {name: '%s', weight:%f})", "Jack", 70.3)

	tx.Commit()

	tx, err = db.Begin()
	if err != nil {
		t.Fatal(err)
	}

	// Match
	mapCursor, err := ExecCypherMap(tx, graphName, 1, "MATCH (n:Person) RETURN n")

	if err != nil {
		t.Fatal(err)
	}

	mapCursor.PutType("Person", reflect.TypeOf(VPerson{}))

	count := 0
	for mapCursor.Next() {
		entities, err := mapCursor.GetRow()
		if err != nil {
			t.Fatal(err)
		}
		count++
		person := entities[0].(VPerson)

		fmt.Println(count, "]", person.Name, person.Age, person.Weight)
	}

	// Create Path
	ExecCypher(tx, graphName, 0, "MATCH (a:Person), (b:Person) WHERE a.name='%s' AND b.name='%s' CREATE (a)-[r:workWith {weight: %d}]->(b)",
		"Jack", "Joe", 3)

	ExecCypher(tx, graphName, 0, "MATCH (a:Person {name: '%s'}), (b:Person {name: '%s'}) CREATE (a)-[r:workWith {weight: %d}]->(b)",
		"Joe", "Smith", 7)

	tx.Commit()

	tx, err = db.Begin()
	if err != nil {
		t.Fatal(err)
	}

	// Query Path
	mapCursor, err = ExecCypherMap(tx, graphName, 3, "MATCH (a)-[b:workWith]-(c) RETURN a,b,c")
	if err != nil {
		t.Fatal(err)
	}

	mapCursor.PutType("Person", reflect.TypeOf(VPerson{}))
	mapCursor.PutType("workWith", reflect.TypeOf(EWorkWith{}))

	count = 0
	for mapCursor.Next() {
		entities, err := mapCursor.GetRow()
		if err != nil {
			t.Fatal(err)
		}
		count++
		person1 := entities[0].(VPerson)
		workWith := entities[1].(EWorkWith)
		person2 := entities[2].(VPerson)

		fmt.Println(count, "]", person1, workWith, person2)
	}

	// Clear Data
	_, err = ExecCypher(tx, graphName, 0, "MATCH (n:Person) DETACH DELETE n RETURN *")
	if err != nil {
		t.Fatal(err)
	}
	tx.Commit()
}

func TestCudReturn(t *testing.T) {
	db, err := sql.Open("postgres", dsn)
	if err != nil {
		t.Fatal(err)
	}

	// Confirm graph_path created
	_, err = GetReady(db, graphName)
	if err != nil {
		t.Fatal(err)
	}

	// Tx begin for execute create vertex
	tx, err := db.Begin()

	if err != nil {
		t.Fatal(err)
	}

	// Create Vertex
	cursor, err := ExecCypher(tx, graphName, 1, "CREATE (n:Person {name: '%s'}) RETURN n", "Joe")

	if err != nil {
		t.Fatal(err)
	}

	for cursor.Next() {
		fmt.Println(cursor.GetRow())
	}

	cursor, err = ExecCypher(tx, graphName, 1, "CREATE (n:Person {name: '%s', age: %d}) RETURN n", "Smith", 10)

	if err != nil {
		t.Fatal(err)
	}

	for cursor.Next() {
		fmt.Println(cursor.GetRow())
	}

	cursor, err = ExecCypher(tx, graphName, 1, "CREATE (n:Person {name: '%s', weight:%f}) RETURN n", "Jack", 70.3)

	if err != nil {
		t.Fatal(err)
	}

	for cursor.Next() {
		fmt.Println(cursor.GetRow())
	}

	tx.Commit()
	tx, err = db.Begin()
	if err != nil {
		t.Fatal(err)
	}

	cursor, err = ExecCypher(tx, graphName, 1, "MATCH (n:Person) RETURN n")

	if err != nil {
		t.Fatal(err)
	}

	for cursor.Next() {
		fmt.Println(cursor.GetRow())
	}

}

func TestQueryManyReturn(t *testing.T) {
	ag, err := ConnectAge(graphName, dsn)

	if err != nil {
		t.Fatal(err)
	}

	tx, err := ag.Begin()
	if err != nil {
		t.Fatal(err)
	}

	// Create Vertex
	tx.ExecCypher(0, "CREATE (n:Person {name: '%s'})", "Joe")
	tx.ExecCypher(0, "CREATE (n:Person {name: '%s', age: %d})", "Smith", 10)
	tx.ExecCypher(0, "CREATE (n:Person {name: '%s', weight:%f})", "Jack", 70.3)

	tx.Commit()

	tx, err = ag.Begin()
	if err != nil {
		t.Fatal(err)
	}

	// Create Path
	tx.ExecCypher(0, "MATCH (a:Person), (b:Person) WHERE a.name='%s' AND b.name='%s' CREATE (a)-[r:workWith {weight: %d}]->(b)",
		"Jack", "Joe", 3)

	tx.ExecCypher(0, "MATCH (a:Person {name: '%s'}), (b:Person {name: '%s'}) CREATE (a)-[r:workWith {weight: %d}]->(b)",
		"Joe", "Smith", 7)

	tx.Commit()

	tx, err = ag.Begin()
	if err != nil {
		t.Fatal(err)
	}

	// Query Path1
	cursor, err := tx.ExecCypher(3, "MATCH (a:Person)-[l:workWith]-(b:Person) RETURN a, l, b")
	if err != nil {
		t.Fatal(err)
	}

	count := 0
	for cursor.Next() {
		entities, err := cursor.GetRow()
		if err != nil {
			t.Fatal(err)
		}
		count++
		v1 := entities[0].(*Vertex)
		edge := entities[1].(*Edge)
		v2 := entities[2].(*Vertex)
		fmt.Println("ROW ", count, ">>", "\n\t", v1, "\n\t", edge, "\n\t", v2)
	}

	// Query Path2
	cursor, err = tx.ExecCypher(1, "MATCH p=(a:Person)-[l:workWith]-(b:Person) WHERE a.name = '%s' RETURN p", "Joe")
	if err != nil {
		t.Fatal(err)
	}

	count = 0
	for cursor.Next() {
		entities, err := cursor.GetRow()
		if err != nil {
			t.Fatal(err)
		}
		count++
		path := entities[0].(*Path)
		fmt.Println("ROW ", count, ">>", "\n\t", path.GetAsVertex(0),
			"\n\t", path.GetAsEdge(1),
			"\n\t", path.GetAsVertex(2))
	}

	// Clear Data
	_, err = tx.ExecCypher(0, "MATCH (n:Person) DETACH DELETE n RETURN *")
	if err != nil {
		t.Fatal(err)
	}
	tx.Commit()
}
