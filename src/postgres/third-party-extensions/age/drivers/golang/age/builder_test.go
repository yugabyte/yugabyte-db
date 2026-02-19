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
	"math"
	"math/big"
	"testing"

	"github.com/stretchr/testify/assert"
)

func TestPathParsing(t *testing.T) {
	rstStr1 := `[{"id": 2251799813685425, "label": "Person", "properties": {"name": "Smith"}}::vertex, 
	{"id": 2533274790396576, "label": "workWith", "end_id": 2251799813685425, "start_id": 2251799813685424, 
		"properties": {"weight": 3, "bigFloat":123456789123456789123456789.12345::numeric}}::edge, 
	{"id": 2251799813685424, "label": "Person", "properties": {"name": "Joe"}}::vertex]::path`

	rstStr2 := `[{"id": 2251799813685424, "label": "Person", "properties": {"name": "Joe"}}::vertex, 
	{"id": 2533274790396576, "label": "workWith", "end_id": 2251799813685425, "start_id": 2251799813685424, "properties": {"weight": 3}}::edge, 
	{"id": 2251799813685425, "label": "Person", "properties": {"name": "Smith"}}::vertex]::path`

	rstStr3 := `[{"id": 2251799813685424, "label": "Person", "properties": {"name": "Joe"}}::vertex, 
	{"id": 2533274790396579, "label": "workWith", "end_id": 2251799813685426, "start_id": 2251799813685424, "properties": {"weight": 5}}::edge, 
	{"id": 2251799813685426, "label": "Person", "properties": {"name": "Jack", "arrVal":["A","B"]}}::vertex]::path`

	unmarshaler := NewAGUnmarshaler()
	entity1, _ := unmarshaler.unmarshal(rstStr1)
	entity2, _ := unmarshaler.unmarshal(rstStr2)
	entity3, _ := unmarshaler.unmarshal(rstStr3)

	assert.Equal(t, entity1.GType(), entity2.GType(), "Type Check")
	p1 := entity1.(*Path)
	p2 := entity2.(*Path)
	p3 := entity3.(*Path)

	assert.Equal(t, p1.GetAsVertex(0).props["name"], p2.GetAsVertex(2).props["name"])
	assert.Equal(t, p2.GetAsVertex(0).props["name"], p3.GetAsVertex(0).props["name"])

	bf := new(big.Float)
	bf, _ = bf.SetString("123456789123456789123456789.12345")

	bigFloat := p1.GetAsEdge(1).props["bigFloat"]

	assert.Equal(t, bf, bigFloat)

	fmt.Println(entity1)
	fmt.Println(entity2)
	fmt.Println(entity3)
}

func TestVertexParsing(t *testing.T) {
	rstStr := `{"id": 2251799813685425, "label": "Person", 
		"properties": {"name": "Smith", "numInt":123, "numIntBig":12345678901235555555555555555::numeric, "numFloat": 384.23424, 
		"yn":true, "nullVal": null}}::vertex`

	unmarshaler := NewAGUnmarshaler()
	entity, _ := unmarshaler.unmarshal(rstStr)

	// fmt.Println(entity)
	assert.Equal(t, G_VERTEX, entity.GType())

	v := entity.(*Vertex)
	assert.Equal(t, "Smith", v.props["name"])
	assert.True(t, (int64(123) == v.props["numInt"]))
	assert.Equal(t, int64(123), v.props["numInt"])

	bi := new(big.Int)
	bi, ok := bi.SetString("12345678901235555555555555555", 10)
	if !ok {
		fmt.Println("Cannot reach this line. ")
	}
	assert.Equal(t, bi, v.props["numIntBig"])

	assert.True(t, (384.23424 == v.props["numFloat"]))
	assert.Equal(t, float64(384.23424), v.props["numFloat"])
	assert.Equal(t, true, v.props["yn"])
	assert.Nil(t, v.props["nullVal"])
}

func TestNormalValueParsing(t *testing.T) {
	mapStr := `{"name": "Smith", "num":123, "yn":true}`
	arrStr := `["name", "Smith", "num", 123, "yn", true]`
	strStr := `"abcd"`
	intStr := `1234`
	floatStr := `1234.56789`
	floatStr2 := `6.45161290322581e+46`
	numericStr1 := `12345678901234567890123456.789::numeric`
	numericStr2 := `12345678901234567890123456::numeric`
	boolStr := `true`
	nullStr := ""
	nanStr := "NaN"
	infpStr := "Infinity"
	infnStr := "-Infinity"

	unmarshaler := NewAGUnmarshaler()
	mapv, _ := unmarshaler.unmarshal(mapStr)
	arrv, _ := unmarshaler.unmarshal(arrStr)
	str2, _ := unmarshaler.unmarshal(strStr)
	intv, _ := unmarshaler.unmarshal(intStr)
	fl, _ := unmarshaler.unmarshal(floatStr)
	fl2, _ := unmarshaler.unmarshal(floatStr2)
	numeric1, _ := unmarshaler.unmarshal(numericStr1)
	numeric2, _ := unmarshaler.unmarshal(numericStr2)
	b, _ := unmarshaler.unmarshal(boolStr)
	nullVal, _ := unmarshaler.unmarshal(nullStr)
	nanVal, _ := unmarshaler.unmarshal(nanStr)
	infpVal, _ := unmarshaler.unmarshal(infpStr)
	infnVal, _ := unmarshaler.unmarshal(infnStr)

	// fmt.Println("intv", intv.GType(), reflect.TypeOf(intv.(*SimpleEntity).Value()), intv)
	assert.Equal(t, G_MAP, mapv.GType())
	assert.Equal(t, G_ARR, arrv.GType())
	assert.Equal(t, G_STR, str2.GType())
	assert.Equal(t, G_INT, intv.GType())
	assert.Equal(t, G_FLOAT, fl.GType())
	assert.Equal(t, G_FLOAT, fl2.GType())
	assert.Equal(t, G_FLOATBIG, numeric1.GType())
	assert.Equal(t, G_INTBIG, numeric2.GType())
	assert.Equal(t, G_BOOL, b.GType())
	assert.Equal(t, G_NULL, nullVal.GType())
	assert.Equal(t, G_FLOAT, nanVal.GType())
	assert.Equal(t, G_FLOAT, infpVal.GType())
	assert.Equal(t, G_FLOAT, infnVal.GType())

	assert.Equal(t, map[string]interface{}{"name": "Smith", "num": int64(123), "yn": true}, mapv.(*SimpleEntity).Value())
	assert.Equal(t, []interface{}{"name", "Smith", "num", int64(123), "yn", true}, arrv.(*SimpleEntity).Value())
	assert.Equal(t, "abcd", str2.(*SimpleEntity).Value())
	assert.Equal(t, int64(1234), intv.(*SimpleEntity).Value())
	assert.Equal(t, 1234.56789, fl.(*SimpleEntity).Value())
	assert.Equal(t, 6.45161290322581e+46, fl2.(*SimpleEntity).Value())
	assert.Equal(t, true, b.(*SimpleEntity).Value())
	assert.Equal(t, nil, nullVal.(*SimpleEntity).Value())
	assert.True(t, math.IsNaN(nanVal.(*SimpleEntity).Value().(float64)))
	assert.Equal(t, math.Inf(1), infpVal.(*SimpleEntity).Value())
	assert.Equal(t, math.Inf(-1), infnVal.(*SimpleEntity).Value())

	bf := new(big.Float)
	bf, _ = bf.SetString("12345678901234567890123456.789")

	assert.Equal(t, bf, numeric1.(*SimpleEntity).Value())

	bi := new(big.Int)
	bi, _ = bi.SetString("12345678901234567890123456", 10)

	assert.Equal(t, bi, numeric2.(*SimpleEntity).Value())
}

func TestMap(t *testing.T) {
	mapStr := `{"name": "Smith", "num":123, "yn":true, "arr":["A","B",1], "map":{"a":1, "b":"bv"}}`

	unmarshaler := NewAGUnmarshaler()
	mapv, _ := unmarshaler.unmarshal(mapStr)
	assert.Equal(t, G_MAP, mapv.GType())

	mapValue := mapv.(*SimpleEntity).Value().(map[string]interface{})

	assert.Equal(t, "Smith", mapValue["name"])
	assert.Equal(t, []interface{}{"A", "B", int64(1)}, mapValue["arr"])
	assert.Equal(t, map[string]interface{}{"a": int64(1), "b": "bv"}, mapValue["map"])
}

func TestArray(t *testing.T) {
	arrayStr := `[ "Smith", 123,  true, ["A","B",1], {"a":1, "b":"bv"}]`

	unmarshaler := NewAGUnmarshaler()
	arrayv, _ := unmarshaler.unmarshal(arrayStr)
	assert.Equal(t, G_ARR, arrayv.GType())

	arrValue := arrayv.(*SimpleEntity).Value().([]interface{})

	assert.Equal(t, "Smith", arrValue[0])
	assert.Equal(t, []interface{}{"A", "B", int64(1)}, arrValue[3])
	assert.Equal(t, map[string]interface{}{"a": int64(1), "b": "bv"}, arrValue[4])
}
