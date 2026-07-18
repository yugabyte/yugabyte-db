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
	"strconv"
	"strings"
	"github.com/antlr/antlr4/runtime/Go/antlr/v4"
	"github.com/apache/age/drivers/golang/parser"
)

const MaxUint = ^uint(0)
const MaxInt = int(MaxUint >> 1)
const MinUint = 0
const MinInt = -MaxInt - 1

type Unmarshaller interface {
	unmarshal(text string) (Entity, error)
}

type AGUnmarshaler struct {
	Unmarshaller
	ageParser   *parser.AgeParser
	visitor     parser.AgeVisitor
	errListener *AGErrorListener
	vcache      map[int64]interface{}
}

func NewAGUnmarshaler() *AGUnmarshaler {
	vcache := make(map[int64]interface{})

	m := &AGUnmarshaler{ageParser: parser.NewAgeParser(nil),
		visitor:     &UnmarshalVisitor{vcache: vcache},
		errListener: NewAGErrorListener(),
		vcache:      vcache,
	}
	m.ageParser.AddErrorListener(m.errListener)

	return m
}

func (p *AGUnmarshaler) unmarshal(text string) (Entity, error) {
	if len(text) == 0 {
		return NewSimpleEntity(nil), nil
	}
	input := antlr.NewInputStream(text)
	lexer := parser.NewAgeLexer(input)
	stream := antlr.NewCommonTokenStream(lexer, 0)
	p.ageParser.SetInputStream(stream)
	tree := p.ageParser.Ageout()
	rst := tree.Accept(p.visitor)

	if len(p.errListener.errList) > 0 {
		var ape *AgeParseError = nil
		errs := make([]string, len(p.errListener.errList))
		for idx, re := range p.errListener.errList {
			errs[idx] = re.GetMessage()
			fmt.Println(re)
		}
		p.errListener.clearErrs()

		ape = &AgeParseError{msg: "Cypher query:" + text, errors: errs}

		return nil, ape
	}

	if !IsEntity(rst) {
		rst = NewSimpleEntity(rst)
	}

	return rst.(Entity), nil
}

type AGErrorListener struct {
	*antlr.DefaultErrorListener
	errList []antlr.RecognitionException
}

func NewAGErrorListener() *AGErrorListener {
	return &AGErrorListener{DefaultErrorListener: &antlr.DefaultErrorListener{}, errList: []antlr.RecognitionException{}}
}

func (el *AGErrorListener) SyntaxError(recognizer antlr.Recognizer, offendingSymbol interface{}, line, column int, msg string, e antlr.RecognitionException) {
	el.errList = append(el.errList, e)
}

func (el *AGErrorListener) getErrs() []antlr.RecognitionException {
	return el.errList
}
func (el *AGErrorListener) clearErrs() {
	el.errList = []antlr.RecognitionException{}
}

type UnmarshalVisitor struct {
	parser.AgeVisitor
	vcache map[int64]interface{}
}

func (v *UnmarshalVisitor) Visit(tree antlr.ParseTree) interface{} { return nil }
func (v *UnmarshalVisitor) VisitChildren(node antlr.RuleNode) interface{} {
	var rtn interface{}
	for _, c := range node.GetChildren() {
		pt := c.(antlr.ParseTree)
		rtn = pt.Accept(v)
	}
	return rtn
}
func (v *UnmarshalVisitor) VisitTerminal(node antlr.TerminalNode) interface{} { return nil }
func (v *UnmarshalVisitor) VisitErrorNode(node antlr.ErrorNode) interface{}   { return nil }
func (v *UnmarshalVisitor) VisitAgeout(ctx *parser.AgeoutContext) interface{} {
	rtn := v.VisitChildren(ctx)
	return rtn
}

// Visit a parse tree produced by AgeParser#vertex.
func (v *UnmarshalVisitor) VisitVertex(ctx *parser.VertexContext) interface{} {
	propCtx := ctx.Properties()
	props := propCtx.Accept(v).(map[string]interface{})
	// fmt.Println(" * VisitVertex:", props)
	vid := int64(props["id"].(int64))
	vertex, ok := v.vcache[vid]

	if !ok {
		vertex = NewVertex(vid, props["label"].(string), props["properties"].(map[string]interface{}))
		v.vcache[vid] = vertex
	}

	return vertex
}

// Visit a parse tree produced by AgeParser#edge.
func (v *UnmarshalVisitor) VisitEdge(ctx *parser.EdgeContext) interface{} {
	propCtx := ctx.Properties()
	props := propCtx.Accept(v).(map[string]interface{})
	// fmt.Println(" * VisitEdge:", props)

	edge := NewEdge(int64(props["id"].(int64)), props["label"].(string),
		int64(props["start_id"].(int64)), int64(props["end_id"].(int64)),
		props["properties"].(map[string]interface{}))

	return edge
}

// Visit a parse tree produced by AgeParser#path.
func (v *UnmarshalVisitor) VisitPath(ctx *parser.PathContext) interface{} {
	entities := []Entity{}

	for _, child := range ctx.GetChildren() {
		switch child.(type) {
		case *parser.VertexContext:
			v := child.(*parser.VertexContext).Accept(v)
			// fmt.Println(v)
			entities = append(entities, v.(Entity))
		case *parser.EdgeContext:
			e := child.(*parser.EdgeContext).Accept(v)
			// fmt.Println(e)
			entities = append(entities, e.(Entity))
		default:
		}
	}

	path := NewPath(entities)
	return path
}

// Visit a parse tree produced by AgeParser#value.
func (v *UnmarshalVisitor) VisitValue(ctx *parser.ValueContext) interface{} {
	child := ctx.GetChild(0)
	switch child.(type) {
	case *antlr.TerminalNodeImpl:
		rtn, err := unmarshalTerm(child.(*antlr.TerminalNodeImpl))
		if err != nil {
			panic(err)
		}
		return rtn
	default:
		return child.(antlr.ParserRuleContext).Accept(v)
	}
}

// Visit a parse tree produced by AgeParser#properties.
func (v *UnmarshalVisitor) VisitProperties(ctx *parser.PropertiesContext) interface{} {
	props := make(map[string]interface{})
	for _, pairCtx := range ctx.AllPair() {
		pairCtx.Accept(v)
		pair := pairCtx.(*parser.PairContext)
		key := strings.Trim(pair.STRING().GetText(), "\"")
		// fmt.Println("Pair KEY:", key)
		value := pair.Value().Accept(v)
		props[key] = value
	}

	return props
}

// Visit a parse tree produced by AgeParser#pair.
func (v *UnmarshalVisitor) VisitPair(ctx *parser.PairContext) interface{} {
	return nil
}

// Visit a parse tree produced by AgeParser#arr.
func (v *UnmarshalVisitor) VisitArr(ctx *parser.ArrContext) interface{} {
	var arr []interface{}
	for _, child := range ctx.GetChildren() {
		switch child.(type) {
		case *antlr.TerminalNodeImpl:
			// skip
			break
		default:
			el := child.(antlr.ParserRuleContext).Accept(v)
			arr = append(arr, el)
		}
	}
	return arr
}

func unmarshalTerm(ctx *antlr.TerminalNodeImpl) (interface{}, error) {
	txt := ctx.GetText()
	switch ctx.GetSymbol().GetTokenType() {
	case parser.AgeLexerSTRING:
		return strings.Trim(txt, "\""), nil
	case parser.AgeLexerNUMERIC:
		numStr := txt[:len(txt)-9]
		// fmt.Println("txt   ", txt)
		// fmt.Println("numStr", numStr)
		if strings.Contains(numStr, ".") {
			bi := new(big.Float)
			bi, ok := bi.SetString(numStr)
			if !ok {
				return nil, &AgeParseError{msg: "Parse big float " + txt}
			}
			return bi, nil
		} else {
			bi := new(big.Int)
			bi, ok := bi.SetString(numStr, 10)
			if !ok {
				return nil, &AgeParseError{msg: "Parse big int " + txt}
			}
			return bi, nil
		}
	case parser.AgeLexerNUMBER:
		if strings.Contains(txt, ".") {
			return strconv.ParseFloat(txt, 64)
		} else {
			return strconv.ParseInt(txt, 10, 64)
		}
	case parser.AgeLexerFLOAT_EXPR:
		switch txt {
		case "NaN":
			return math.NaN(), nil
		case "-Infinity":
			return math.Inf(-1), nil
		case "Infinity":
			return math.Inf(1), nil
		default:
			return nil, &AgeParseError{msg: "Unknown float expression" + txt}
		}
	case parser.AgeLexerBOOL:
		s, err := strconv.ParseBool(txt)
		if err != nil {
			return nil, err
		} else {
			return s, nil
		}
	case parser.AgeLexerNULL:
		return nil, nil
	default:
		return nil, nil
	}
}
