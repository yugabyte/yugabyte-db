// Code generated from Age.g4 by ANTLR 4.9.2. DO NOT EDIT.

package parser // Age

import "github.com/antlr/antlr4/runtime/Go/antlr"

type BaseAgeVisitor struct {
	*antlr.BaseParseTreeVisitor
}

func (v *BaseAgeVisitor) VisitAgeout(ctx *AgeoutContext) interface{} {
	return v.VisitChildren(ctx)
}

func (v *BaseAgeVisitor) VisitVertex(ctx *VertexContext) interface{} {
	return v.VisitChildren(ctx)
}

func (v *BaseAgeVisitor) VisitEdge(ctx *EdgeContext) interface{} {
	return v.VisitChildren(ctx)
}

func (v *BaseAgeVisitor) VisitPath(ctx *PathContext) interface{} {
	return v.VisitChildren(ctx)
}

func (v *BaseAgeVisitor) VisitValue(ctx *ValueContext) interface{} {
	return v.VisitChildren(ctx)
}

func (v *BaseAgeVisitor) VisitProperties(ctx *PropertiesContext) interface{} {
	return v.VisitChildren(ctx)
}

func (v *BaseAgeVisitor) VisitPair(ctx *PairContext) interface{} {
	return v.VisitChildren(ctx)
}

func (v *BaseAgeVisitor) VisitArr(ctx *ArrContext) interface{} {
	return v.VisitChildren(ctx)
}
