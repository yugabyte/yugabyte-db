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
package main

import (
	"fmt"

	"github.com/apache/age/drivers/golang/age"
)

// Do cypher query to AGE with Age API
func doWithAgeWrapper(dsn string, graphName string) {

	ag, err := age.ConnectAge(graphName, dsn)

	if err != nil {
		panic(err)
	}

	tx, err := ag.Begin()
	if err != nil {
		panic(err)
	}

	_, err = tx.ExecCypher(0, "CREATE (n:Person {name: '%s'})", "Joe")
	if err != nil {
		panic(err)
	}

	_, err = tx.ExecCypher(0, "CREATE (n:Person {name: '%s', age: %d})", "Smith", 10)
	if err != nil {
		panic(err)
	}

	_, err = tx.ExecCypher(0, "CREATE (n:Person {name: '%s', weight:%f})", "Jack", 70.3)
	if err != nil {
		panic(err)
	}

	tx.Commit()

	tx, err = ag.Begin()
	if err != nil {
		panic(err)
	}

	cursor, err := tx.ExecCypher(1, "MATCH (n:Person) RETURN n")
	if err != nil {
		panic(err)
	}

	count := 0
	for cursor.Next() {
		entities, err := cursor.GetRow()
		if err != nil {
			panic(err)
		}
		count++
		vertex := entities[0].(*age.Vertex)
		fmt.Println(count, "]", vertex.Id(), vertex.Label(), vertex.Props())
	}

	fmt.Println("Vertex Count:", count)

	_, err = tx.ExecCypher(0, "MATCH (a:Person), (b:Person) WHERE a.name='%s' AND b.name='%s' CREATE (a)-[r:workWith {weight: %d}]->(b)",
		"Jack", "Joe", 3)
	if err != nil {
		panic(err)
	}

	_, err = tx.ExecCypher(0, "MATCH (a:Person {name: '%s'}), (b:Person {name: '%s'}) CREATE (a)-[r:workWith {weight: %d}]->(b)",
		"Joe", "Smith", 7)
	if err != nil {
		panic(err)
	}

	tx.Commit()

	tx, err = ag.Begin()
	if err != nil {
		panic(err)
	}

	cursor, err = tx.ExecCypher(1, "MATCH p=()-[:workWith]-() RETURN p")
	if err != nil {
		panic(err)
	}

	count = 0
	for cursor.Next() {
		entities, err := cursor.GetRow()
		if err != nil {
			panic(err)
		}
		count++

		path := entities[0].(*age.Path)

		vertexStart := path.GetAsVertex(0)
		edge := path.GetAsEdge(1)
		vertexEnd := path.GetAsVertex(2)

		fmt.Println(count, "]", vertexStart, edge.Props(), vertexEnd)
	}

	// Query with return many columns
	cursor, err = tx.ExecCypher(3, "MATCH (a:Person)-[l:workWith]-(b:Person) RETURN a, l, b")
	if err != nil {
		panic(err)
	}

	count = 0
	for cursor.Next() {
		row, err := cursor.GetRow()
		if err != nil {
			panic(err)
		}

		count++

		v1 := row[0].(*age.Vertex)
		edge := row[1].(*age.Edge)
		v2 := row[2].(*age.Vertex)

		fmt.Println("ROW ", count, ">>", "\n\t", v1, "\n\t", edge, "\n\t", v2)
	}

	_, err = tx.ExecCypher(0, "MATCH (n:Person) DETACH DELETE n RETURN *")
	if err != nil {
		panic(err)
	}
	tx.Commit()
}
