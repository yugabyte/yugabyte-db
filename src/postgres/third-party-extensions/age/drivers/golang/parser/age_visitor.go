// Code generated from java-escape by ANTLR 4.11.1. DO NOT EDIT.

package parser // Age

import "github.com/antlr/antlr4/runtime/Go/antlr/v4"

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
