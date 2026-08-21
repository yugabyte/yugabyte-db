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
	"bytes"
	"database/sql"
	"fmt"
	"reflect"
)

// GetReady prepare AGE extension
// load AGE extension
// set graph path
func GetReady(db *sql.DB, graphName string) (bool, error) {
	tx, err := db.Begin()
	if err != nil {
		return false, err
	}

	_, err = tx.Exec("LOAD 'age';")
	if err != nil {
		return false, err
	}

	_, err = tx.Exec("SET search_path = ag_catalog, '$user', public;")
	if err != nil {
		return false, err
	}

	var count int = 0

	err = tx.QueryRow("SELECT count(*) FROM ag_graph WHERE name=$1", graphName).Scan(&count)

	if err != nil {
		return false, err
	}

	if count == 0 {
		_, err = tx.Exec("SELECT create_graph($1);", graphName)
		if err != nil {
			return false, err
		}
	}

	tx.Commit()

	return true, nil
}

type CursorProvider func(columnCount int, rows *sql.Rows) Cursor

type Cursor interface {
	Next() bool
	Close() error
}

func execCypher(cursorProvider CursorProvider, tx *sql.Tx, graphName string, columnCount int, cypher string, args ...interface{}) (Cursor, error) {
	var buf bytes.Buffer

	cypherStmt := fmt.Sprintf(cypher, args...)

        buf.WriteString("SELECT * from cypher(NULL,NULL) as (v0 agtype")

	for i := 1; i < columnCount; i++ {
		buf.WriteString(fmt.Sprintf(", v%d agtype", i))
	}
	buf.WriteString(");")

	stmt := buf.String()

        // Pass in the graph name and cypher statement via parameters to prepare
        // the cypher function call for session info.

        prepare_stmt := "SELECT * FROM age_prepare_cypher($1, $2);"
        _, perr := tx.Exec(prepare_stmt, graphName, cypherStmt)
        if perr != nil {
                        fmt.Println(prepare_stmt + " " + graphName + " " + cypher)
                        return nil, perr
        }

	if columnCount == 0 {
                _, err := tx.Exec(stmt)
		if err != nil {
			fmt.Println(stmt)
			return nil, err
		}
		return nil, nil
	} else {
                rows, err := tx.Query(stmt)
		if err != nil {
			fmt.Println(stmt)
			return nil, err
		}
		return cursorProvider(columnCount, rows), nil
	}
}

// ExecCypher : execute cypher query
// CREATE , DROP ....
// MATCH .... RETURN ....
// CREATE , DROP .... RETURN ...
func ExecCypher(tx *sql.Tx, graphName string, columnCount int, cypher string, args ...interface{}) (*CypherCursor, error) {
	cursor, err := execCypher(NewCypherCursor, tx, graphName, columnCount, cypher, args...)
	var cypherCursor *CypherCursor
	if cursor != nil {
		cypherCursor = cursor.(*CypherCursor)
	}
	return cypherCursor, err
}

// ExecCypherMap
// CREATE , DROP ....
// MATCH .... RETURN ....
// CREATE , DROP .... RETURN ...
func ExecCypherMap(tx *sql.Tx, graphName string, columnCount int, cypher string, args ...interface{}) (*CypherMapCursor, error) {
	cursor, err := execCypher(NewCypherMapCursor, tx, graphName, columnCount, cypher, args...)
	var cypherMapCursor *CypherMapCursor
	if cursor != nil {
		cypherMapCursor = cursor.(*CypherMapCursor)
	}
	return cypherMapCursor, err
}

type Age struct {
	db        *sql.DB
	graphName string
}

type AgeTx struct {
	age *Age
	tx  *sql.Tx
}

/**
@param dsn host=127.0.0.1 port=5432 dbname=postgres user=postgres password=agens sslmode=disable
*/
func ConnectAge(graphName string, dsn string) (*Age, error) {
	db, err := sql.Open("postgres", dsn)
	if err != nil {
		return nil, err
	}
	age := &Age{db: db, graphName: graphName}
	_, err = age.GetReady()

	if err != nil {
		db.Close()
		age = nil
	}

	return age, err
}

func NewAge(graphName string, db *sql.DB) *Age {
	return &Age{db: db, graphName: graphName}
}

func (age *Age) GetReady() (bool, error) {
	tx, err := age.db.Begin()
	if err != nil {
		return false, err
	}

	_, err = tx.Exec("LOAD 'age';")
	if err != nil {
		return false, err
	}

	_, err = tx.Exec("SET search_path = ag_catalog, '$user', public;")
	if err != nil {
		return false, err
	}

	var count int = 0

	err = tx.QueryRow("SELECT count(*) FROM ag_graph WHERE name=$1", age.graphName).Scan(&count)

	if err != nil {
		return false, err
	}

	if count == 0 {
		_, err = tx.Exec("SELECT create_graph($1);", age.graphName)
		if err != nil {
			return false, err
		}
	}

	tx.Commit()

	return true, nil
}

func (a *Age) Close() error {
	return a.db.Close()
}

func (a *Age) DB() *sql.DB {
	return a.db
}

func (a *Age) Begin() (*AgeTx, error) {
	ageTx := &AgeTx{age: a}
	tx, err := a.db.Begin()
	if err != nil {
		return nil, err
	}
	ageTx.tx = tx
	return ageTx, err
}

func (t *AgeTx) Commit() error {
	return t.tx.Commit()
}

func (t *AgeTx) Rollback() error {
	return t.tx.Rollback()
}

/** CREATE , DROP .... */
func (a *AgeTx) ExecCypher(columnCount int, cypher string, args ...interface{}) (*CypherCursor, error) {
	return ExecCypher(a.tx, a.age.graphName, columnCount, cypher, args...)
}

func (a *AgeTx) ExecCypherMap(columnCount int, cypher string, args ...interface{}) (*CypherMapCursor, error) {
	return ExecCypherMap(a.tx, a.age.graphName, columnCount, cypher, args...)
}

type CypherCursor struct {
	Cursor
	columnCount int
	rows        *sql.Rows
	unmarshaler Unmarshaller
}

func NewCypherCursor(columnCount int, rows *sql.Rows) Cursor {
	return &CypherCursor{columnCount: columnCount, rows: rows, unmarshaler: NewAGUnmarshaler()}
}

func (c *CypherCursor) Next() bool {
	return c.rows.Next()
}

func (c *CypherCursor) GetRow() ([]Entity, error) {
	var gstrs = make([]interface{}, c.columnCount)
	for i := 0; i < c.columnCount; i++ {
		gstrs[i] = new(string)
	}

	err := c.rows.Scan(gstrs...)
	if err != nil {
		return nil, fmt.Errorf("CypherCursor.GetRow:: %s", err)
	}

	entArr := make([]Entity, c.columnCount)
	for i := 0; i < c.columnCount; i++ {
		gstr := gstrs[i].(*string)
		e, err := c.unmarshaler.unmarshal(*gstr)
		if err != nil {
			fmt.Println(i, ">>", gstr)
			return nil, err
		}
		entArr[i] = e
	}

	return entArr, nil
}

func (c *CypherCursor) Close() error {
	return c.rows.Close()
}

//
type CypherMapCursor struct {
	CypherCursor
	mapper *AGMapper
}

func NewCypherMapCursor(columnCount int, rows *sql.Rows) Cursor {
	mapper := NewAGMapper(make(map[string]reflect.Type))
	pcursor := CypherCursor{columnCount: columnCount, rows: rows, unmarshaler: mapper}
	return &CypherMapCursor{CypherCursor: pcursor, mapper: mapper}
}

func (c *CypherMapCursor) PutType(label string, tp reflect.Type) {
	c.mapper.PutType(label, tp)
}

func (c *CypherMapCursor) GetRow() ([]interface{}, error) {
	entities, err := c.CypherCursor.GetRow()

	if err != nil {
		return nil, fmt.Errorf("CypherMapCursor.GetRow:: %s", err)
	}

	elArr := make([]interface{}, c.columnCount)

	for i := 0; i < c.columnCount; i++ {
		ent := entities[i]
		if ent.GType() == G_MAP_PATH {
			elArr[i] = ent
		} else {
			elArr[i] = ent.(*SimpleEntity).Value()
		}
	}

	return elArr, nil
}
