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
// Code generated from Age.g4 by ANTLR 4.9.2. DO NOT EDIT.

package parser // Age

import "github.com/antlr/antlr4/runtime/Go/antlr"

// A complete Visitor for a parse tree produced by AgeParser.
type AgeVisitor interface {
	antlr.ParseTreeVisitor

	// Visit a parse tree produced by AgeParser#ageout.
	VisitAgeout(ctx *AgeoutContext) interface{}

	// Visit a parse tree produced by AgeParser#vertex.
	VisitVertex(ctx *VertexContext) interface{}

	// Visit a parse tree produced by AgeParser#edge.
	VisitEdge(ctx *EdgeContext) interface{}

	// Visit a parse tree produced by AgeParser#path.
	VisitPath(ctx *PathContext) interface{}

	// Visit a parse tree produced by AgeParser#value.
	VisitValue(ctx *ValueContext) interface{}

	// Visit a parse tree produced by AgeParser#properties.
	VisitProperties(ctx *PropertiesContext) interface{}

	// Visit a parse tree produced by AgeParser#pair.
	VisitPair(ctx *PairContext) interface{}

	// Visit a parse tree produced by AgeParser#arr.
	VisitArr(ctx *ArrContext) interface{}
}
