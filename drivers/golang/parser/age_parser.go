// Code generated from java-escape by ANTLR 4.11.1. DO NOT EDIT.

package parser // Age

import (
	"fmt"
	"strconv"
	"sync"

	"github.com/antlr/antlr4/runtime/Go/antlr/v4"
)

// Suppress unused import errors
var _ = fmt.Printf
var _ = strconv.Itoa
var _ = sync.Once{}

type AgeParser struct {
	*antlr.BaseParser
}

var ageParserStaticData struct {
	once                   sync.Once
	serializedATN          []int32
	literalNames           []string
	symbolicNames          []string
	ruleNames              []string
	predictionContextCache *antlr.PredictionContextCache
	atn                    *antlr.ATN
	decisionToDFA          []*antlr.DFA
}

func ageParserInit() {
	staticData := &ageParserStaticData
	staticData.literalNames = []string{
		"", "'['", "','", "']'", "'{'", "'}'", "':'", "'::vertex'", "'::edge'",
		"'::path'", "'::numeric'", "", "", "'null'",
	}
	staticData.symbolicNames = []string{
		"", "", "", "", "", "", "", "KW_VERTEX", "KW_EDGE", "KW_PATH", "KW_NUMERIC",
		"STRING", "BOOL", "NULL", "NUMBER", "FLOAT_EXPR", "NUMERIC", "WS",
	}
	staticData.ruleNames = []string{
		"ageout", "vertex", "edge", "path", "value", "properties", "pair", "arr",
	}
	staticData.predictionContextCache = antlr.NewPredictionContextCache()
	staticData.serializedATN = []int32{
		4, 1, 17, 88, 2, 0, 7, 0, 2, 1, 7, 1, 2, 2, 7, 2, 2, 3, 7, 3, 2, 4, 7,
		4, 2, 5, 7, 5, 2, 6, 7, 6, 2, 7, 7, 7, 1, 0, 1, 0, 1, 0, 1, 0, 3, 0, 21,
		8, 0, 1, 1, 1, 1, 1, 1, 1, 2, 1, 2, 1, 2, 1, 3, 1, 3, 1, 3, 1, 3, 1, 3,
		1, 3, 1, 3, 5, 3, 36, 8, 3, 10, 3, 12, 3, 39, 9, 3, 1, 3, 1, 3, 1, 3, 1,
		4, 1, 4, 1, 4, 1, 4, 1, 4, 1, 4, 1, 4, 1, 4, 3, 4, 52, 8, 4, 1, 5, 1, 5,
		1, 5, 1, 5, 5, 5, 58, 8, 5, 10, 5, 12, 5, 61, 9, 5, 1, 5, 1, 5, 1, 5, 1,
		5, 3, 5, 67, 8, 5, 1, 6, 1, 6, 1, 6, 1, 6, 1, 7, 1, 7, 1, 7, 1, 7, 5, 7,
		77, 8, 7, 10, 7, 12, 7, 80, 9, 7, 1, 7, 1, 7, 1, 7, 1, 7, 3, 7, 86, 8,
		7, 1, 7, 0, 0, 8, 0, 2, 4, 6, 8, 10, 12, 14, 0, 0, 94, 0, 20, 1, 0, 0,
		0, 2, 22, 1, 0, 0, 0, 4, 25, 1, 0, 0, 0, 6, 28, 1, 0, 0, 0, 8, 51, 1, 0,
		0, 0, 10, 66, 1, 0, 0, 0, 12, 68, 1, 0, 0, 0, 14, 85, 1, 0, 0, 0, 16, 21,
		3, 8, 4, 0, 17, 21, 3, 2, 1, 0, 18, 21, 3, 4, 2, 0, 19, 21, 3, 6, 3, 0,
		20, 16, 1, 0, 0, 0, 20, 17, 1, 0, 0, 0, 20, 18, 1, 0, 0, 0, 20, 19, 1,
		0, 0, 0, 21, 1, 1, 0, 0, 0, 22, 23, 3, 10, 5, 0, 23, 24, 5, 7, 0, 0, 24,
		3, 1, 0, 0, 0, 25, 26, 3, 10, 5, 0, 26, 27, 5, 8, 0, 0, 27, 5, 1, 0, 0,
		0, 28, 29, 5, 1, 0, 0, 29, 37, 3, 2, 1, 0, 30, 31, 5, 2, 0, 0, 31, 32,
		3, 4, 2, 0, 32, 33, 5, 2, 0, 0, 33, 34, 3, 2, 1, 0, 34, 36, 1, 0, 0, 0,
		35, 30, 1, 0, 0, 0, 36, 39, 1, 0, 0, 0, 37, 35, 1, 0, 0, 0, 37, 38, 1,
		0, 0, 0, 38, 40, 1, 0, 0, 0, 39, 37, 1, 0, 0, 0, 40, 41, 5, 3, 0, 0, 41,
		42, 5, 9, 0, 0, 42, 7, 1, 0, 0, 0, 43, 52, 5, 11, 0, 0, 44, 52, 5, 14,
		0, 0, 45, 52, 5, 16, 0, 0, 46, 52, 5, 15, 0, 0, 47, 52, 5, 12, 0, 0, 48,
		52, 5, 13, 0, 0, 49, 52, 3, 10, 5, 0, 50, 52, 3, 14, 7, 0, 51, 43, 1, 0,
		0, 0, 51, 44, 1, 0, 0, 0, 51, 45, 1, 0, 0, 0, 51, 46, 1, 0, 0, 0, 51, 47,
		1, 0, 0, 0, 51, 48, 1, 0, 0, 0, 51, 49, 1, 0, 0, 0, 51, 50, 1, 0, 0, 0,
		52, 9, 1, 0, 0, 0, 53, 54, 5, 4, 0, 0, 54, 59, 3, 12, 6, 0, 55, 56, 5,
		2, 0, 0, 56, 58, 3, 12, 6, 0, 57, 55, 1, 0, 0, 0, 58, 61, 1, 0, 0, 0, 59,
		57, 1, 0, 0, 0, 59, 60, 1, 0, 0, 0, 60, 62, 1, 0, 0, 0, 61, 59, 1, 0, 0,
		0, 62, 63, 5, 5, 0, 0, 63, 67, 1, 0, 0, 0, 64, 65, 5, 4, 0, 0, 65, 67,
		5, 5, 0, 0, 66, 53, 1, 0, 0, 0, 66, 64, 1, 0, 0, 0, 67, 11, 1, 0, 0, 0,
		68, 69, 5, 11, 0, 0, 69, 70, 5, 6, 0, 0, 70, 71, 3, 8, 4, 0, 71, 13, 1,
		0, 0, 0, 72, 73, 5, 1, 0, 0, 73, 78, 3, 8, 4, 0, 74, 75, 5, 2, 0, 0, 75,
		77, 3, 8, 4, 0, 76, 74, 1, 0, 0, 0, 77, 80, 1, 0, 0, 0, 78, 76, 1, 0, 0,
		0, 78, 79, 1, 0, 0, 0, 79, 81, 1, 0, 0, 0, 80, 78, 1, 0, 0, 0, 81, 82,
		5, 3, 0, 0, 82, 86, 1, 0, 0, 0, 83, 84, 5, 1, 0, 0, 84, 86, 5, 3, 0, 0,
		85, 72, 1, 0, 0, 0, 85, 83, 1, 0, 0, 0, 86, 15, 1, 0, 0, 0, 7, 20, 37,
		51, 59, 66, 78, 85,
	}
	deserializer := antlr.NewATNDeserializer(nil)
	staticData.atn = deserializer.Deserialize(staticData.serializedATN)
	atn := staticData.atn
	staticData.decisionToDFA = make([]*antlr.DFA, len(atn.DecisionToState))
	decisionToDFA := staticData.decisionToDFA
	for index, state := range atn.DecisionToState {
		decisionToDFA[index] = antlr.NewDFA(state, index)
	}
}

// AgeParserInit initializes any static state used to implement AgeParser. By default the
// static state used to implement the parser is lazily initialized during the first call to
// NewAgeParser(). You can call this function if you wish to initialize the static state ahead
// of time.
func AgeParserInit() {
	staticData := &ageParserStaticData
	staticData.once.Do(ageParserInit)
}

// NewAgeParser produces a new parser instance for the optional input antlr.TokenStream.
func NewAgeParser(input antlr.TokenStream) *AgeParser {
	AgeParserInit()
	this := new(AgeParser)
	this.BaseParser = antlr.NewBaseParser(input)
	staticData := &ageParserStaticData
	this.Interpreter = antlr.NewParserATNSimulator(this, staticData.atn, staticData.decisionToDFA, staticData.predictionContextCache)
	this.RuleNames = staticData.ruleNames
	this.LiteralNames = staticData.literalNames
	this.SymbolicNames = staticData.symbolicNames
	this.GrammarFileName = "java-escape"

	return this
}

// AgeParser tokens.
const (
	AgeParserEOF        = antlr.TokenEOF
	AgeParserT__0       = 1
	AgeParserT__1       = 2
	AgeParserT__2       = 3
	AgeParserT__3       = 4
	AgeParserT__4       = 5
	AgeParserT__5       = 6
	AgeParserKW_VERTEX  = 7
	AgeParserKW_EDGE    = 8
	AgeParserKW_PATH    = 9
	AgeParserKW_NUMERIC = 10
	AgeParserSTRING     = 11
	AgeParserBOOL       = 12
	AgeParserNULL       = 13
	AgeParserNUMBER     = 14
	AgeParserFLOAT_EXPR = 15
	AgeParserNUMERIC    = 16
	AgeParserWS         = 17
)

// AgeParser rules.
const (
	AgeParserRULE_ageout     = 0
	AgeParserRULE_vertex     = 1
	AgeParserRULE_edge       = 2
	AgeParserRULE_path       = 3
	AgeParserRULE_value      = 4
	AgeParserRULE_properties = 5
	AgeParserRULE_pair       = 6
	AgeParserRULE_arr        = 7
)

// IAgeoutContext is an interface to support dynamic dispatch.
type IAgeoutContext interface {
	antlr.ParserRuleContext

	// GetParser returns the parser.
	GetParser() antlr.Parser

	// IsAgeoutContext differentiates from other interfaces.
	IsAgeoutContext()
}

type AgeoutContext struct {
	*antlr.BaseParserRuleContext
	parser antlr.Parser
}

func NewEmptyAgeoutContext() *AgeoutContext {
	var p = new(AgeoutContext)
	p.BaseParserRuleContext = antlr.NewBaseParserRuleContext(nil, -1)
	p.RuleIndex = AgeParserRULE_ageout
	return p
}

func (*AgeoutContext) IsAgeoutContext() {}

func NewAgeoutContext(parser antlr.Parser, parent antlr.ParserRuleContext, invokingState int) *AgeoutContext {
	var p = new(AgeoutContext)

	p.BaseParserRuleContext = antlr.NewBaseParserRuleContext(parent, invokingState)

	p.parser = parser
	p.RuleIndex = AgeParserRULE_ageout

	return p
}

func (s *AgeoutContext) GetParser() antlr.Parser { return s.parser }

func (s *AgeoutContext) Value() IValueContext {
	var t antlr.RuleContext
	for _, ctx := range s.GetChildren() {
		if _, ok := ctx.(IValueContext); ok {
			t = ctx.(antlr.RuleContext)
			break
		}
	}

	if t == nil {
		return nil
	}

	return t.(IValueContext)
}

func (s *AgeoutContext) Vertex() IVertexContext {
	var t antlr.RuleContext
	for _, ctx := range s.GetChildren() {
		if _, ok := ctx.(IVertexContext); ok {
			t = ctx.(antlr.RuleContext)
			break
		}
	}

	if t == nil {
		return nil
	}

	return t.(IVertexContext)
}

func (s *AgeoutContext) Edge() IEdgeContext {
	var t antlr.RuleContext
	for _, ctx := range s.GetChildren() {
		if _, ok := ctx.(IEdgeContext); ok {
			t = ctx.(antlr.RuleContext)
			break
		}
	}

	if t == nil {
		return nil
	}

	return t.(IEdgeContext)
}

func (s *AgeoutContext) Path() IPathContext {
	var t antlr.RuleContext
	for _, ctx := range s.GetChildren() {
		if _, ok := ctx.(IPathContext); ok {
			t = ctx.(antlr.RuleContext)
			break
		}
	}

	if t == nil {
		return nil
	}

	return t.(IPathContext)
}

func (s *AgeoutContext) GetRuleContext() antlr.RuleContext {
	return s
}

func (s *AgeoutContext) ToStringTree(ruleNames []string, recog antlr.Recognizer) string {
	return antlr.TreesStringTree(s, ruleNames, recog)
}

func (s *AgeoutContext) EnterRule(listener antlr.ParseTreeListener) {
	if listenerT, ok := listener.(AgeListener); ok {
		listenerT.EnterAgeout(s)
	}
}

func (s *AgeoutContext) ExitRule(listener antlr.ParseTreeListener) {
	if listenerT, ok := listener.(AgeListener); ok {
		listenerT.ExitAgeout(s)
	}
}

func (s *AgeoutContext) Accept(visitor antlr.ParseTreeVisitor) interface{} {
	switch t := visitor.(type) {
	case AgeVisitor:
		return t.VisitAgeout(s)

	default:
		return t.VisitChildren(s)
	}
}

func (p *AgeParser) Ageout() (localctx IAgeoutContext) {
	this := p
	_ = this

	localctx = NewAgeoutContext(p, p.GetParserRuleContext(), p.GetState())
	p.EnterRule(localctx, 0, AgeParserRULE_ageout)

	defer func() {
		p.ExitRule()
	}()

	defer func() {
		if err := recover(); err != nil {
			if v, ok := err.(antlr.RecognitionException); ok {
				localctx.SetException(v)
				p.GetErrorHandler().ReportError(p, v)
				p.GetErrorHandler().Recover(p, v)
			} else {
				panic(err)
			}
		}
	}()

	p.SetState(20)
	p.GetErrorHandler().Sync(p)
	switch p.GetInterpreter().AdaptivePredict(p.GetTokenStream(), 0, p.GetParserRuleContext()) {
	case 1:
		p.EnterOuterAlt(localctx, 1)
		{
			p.SetState(16)
			p.Value()
		}

	case 2:
		p.EnterOuterAlt(localctx, 2)
		{
			p.SetState(17)
			p.Vertex()
		}

	case 3:
		p.EnterOuterAlt(localctx, 3)
		{
			p.SetState(18)
			p.Edge()
		}

	case 4:
		p.EnterOuterAlt(localctx, 4)
		{
			p.SetState(19)
			p.Path()
		}

	}

	return localctx
}

// IVertexContext is an interface to support dynamic dispatch.
type IVertexContext interface {
	antlr.ParserRuleContext

	// GetParser returns the parser.
	GetParser() antlr.Parser

	// IsVertexContext differentiates from other interfaces.
	IsVertexContext()
}

type VertexContext struct {
	*antlr.BaseParserRuleContext
	parser antlr.Parser
}

func NewEmptyVertexContext() *VertexContext {
	var p = new(VertexContext)
	p.BaseParserRuleContext = antlr.NewBaseParserRuleContext(nil, -1)
	p.RuleIndex = AgeParserRULE_vertex
	return p
}

func (*VertexContext) IsVertexContext() {}

func NewVertexContext(parser antlr.Parser, parent antlr.ParserRuleContext, invokingState int) *VertexContext {
	var p = new(VertexContext)

	p.BaseParserRuleContext = antlr.NewBaseParserRuleContext(parent, invokingState)

	p.parser = parser
	p.RuleIndex = AgeParserRULE_vertex

	return p
}

func (s *VertexContext) GetParser() antlr.Parser { return s.parser }

func (s *VertexContext) Properties() IPropertiesContext {
	var t antlr.RuleContext
	for _, ctx := range s.GetChildren() {
		if _, ok := ctx.(IPropertiesContext); ok {
			t = ctx.(antlr.RuleContext)
			break
		}
	}

	if t == nil {
		return nil
	}

	return t.(IPropertiesContext)
}

func (s *VertexContext) KW_VERTEX() antlr.TerminalNode {
	return s.GetToken(AgeParserKW_VERTEX, 0)
}

func (s *VertexContext) GetRuleContext() antlr.RuleContext {
	return s
}

func (s *VertexContext) ToStringTree(ruleNames []string, recog antlr.Recognizer) string {
	return antlr.TreesStringTree(s, ruleNames, recog)
}

func (s *VertexContext) EnterRule(listener antlr.ParseTreeListener) {
	if listenerT, ok := listener.(AgeListener); ok {
		listenerT.EnterVertex(s)
	}
}

func (s *VertexContext) ExitRule(listener antlr.ParseTreeListener) {
	if listenerT, ok := listener.(AgeListener); ok {
		listenerT.ExitVertex(s)
	}
}

func (s *VertexContext) Accept(visitor antlr.ParseTreeVisitor) interface{} {
	switch t := visitor.(type) {
	case AgeVisitor:
		return t.VisitVertex(s)

	default:
		return t.VisitChildren(s)
	}
}

func (p *AgeParser) Vertex() (localctx IVertexContext) {
	this := p
	_ = this

	localctx = NewVertexContext(p, p.GetParserRuleContext(), p.GetState())
	p.EnterRule(localctx, 2, AgeParserRULE_vertex)

	defer func() {
		p.ExitRule()
	}()

	defer func() {
		if err := recover(); err != nil {
			if v, ok := err.(antlr.RecognitionException); ok {
				localctx.SetException(v)
				p.GetErrorHandler().ReportError(p, v)
				p.GetErrorHandler().Recover(p, v)
			} else {
				panic(err)
			}
		}
	}()

	p.EnterOuterAlt(localctx, 1)
	{
		p.SetState(22)
		p.Properties()
	}
	{
		p.SetState(23)
		p.Match(AgeParserKW_VERTEX)
	}

	return localctx
}

// IEdgeContext is an interface to support dynamic dispatch.
type IEdgeContext interface {
	antlr.ParserRuleContext

	// GetParser returns the parser.
	GetParser() antlr.Parser

	// IsEdgeContext differentiates from other interfaces.
	IsEdgeContext()
}

type EdgeContext struct {
	*antlr.BaseParserRuleContext
	parser antlr.Parser
}

func NewEmptyEdgeContext() *EdgeContext {
	var p = new(EdgeContext)
	p.BaseParserRuleContext = antlr.NewBaseParserRuleContext(nil, -1)
	p.RuleIndex = AgeParserRULE_edge
	return p
}

func (*EdgeContext) IsEdgeContext() {}

func NewEdgeContext(parser antlr.Parser, parent antlr.ParserRuleContext, invokingState int) *EdgeContext {
	var p = new(EdgeContext)

	p.BaseParserRuleContext = antlr.NewBaseParserRuleContext(parent, invokingState)

	p.parser = parser
	p.RuleIndex = AgeParserRULE_edge

	return p
}

func (s *EdgeContext) GetParser() antlr.Parser { return s.parser }

func (s *EdgeContext) Properties() IPropertiesContext {
	var t antlr.RuleContext
	for _, ctx := range s.GetChildren() {
		if _, ok := ctx.(IPropertiesContext); ok {
			t = ctx.(antlr.RuleContext)
			break
		}
	}

	if t == nil {
		return nil
	}

	return t.(IPropertiesContext)
}

func (s *EdgeContext) KW_EDGE() antlr.TerminalNode {
	return s.GetToken(AgeParserKW_EDGE, 0)
}

func (s *EdgeContext) GetRuleContext() antlr.RuleContext {
	return s
}

func (s *EdgeContext) ToStringTree(ruleNames []string, recog antlr.Recognizer) string {
	return antlr.TreesStringTree(s, ruleNames, recog)
}

func (s *EdgeContext) EnterRule(listener antlr.ParseTreeListener) {
	if listenerT, ok := listener.(AgeListener); ok {
		listenerT.EnterEdge(s)
	}
}

func (s *EdgeContext) ExitRule(listener antlr.ParseTreeListener) {
	if listenerT, ok := listener.(AgeListener); ok {
		listenerT.ExitEdge(s)
	}
}

func (s *EdgeContext) Accept(visitor antlr.ParseTreeVisitor) interface{} {
	switch t := visitor.(type) {
	case AgeVisitor:
		return t.VisitEdge(s)

	default:
		return t.VisitChildren(s)
	}
}

func (p *AgeParser) Edge() (localctx IEdgeContext) {
	this := p
	_ = this

	localctx = NewEdgeContext(p, p.GetParserRuleContext(), p.GetState())
	p.EnterRule(localctx, 4, AgeParserRULE_edge)

	defer func() {
		p.ExitRule()
	}()

	defer func() {
		if err := recover(); err != nil {
			if v, ok := err.(antlr.RecognitionException); ok {
				localctx.SetException(v)
				p.GetErrorHandler().ReportError(p, v)
				p.GetErrorHandler().Recover(p, v)
			} else {
				panic(err)
			}
		}
	}()

	p.EnterOuterAlt(localctx, 1)
	{
		p.SetState(25)
		p.Properties()
	}
	{
		p.SetState(26)
		p.Match(AgeParserKW_EDGE)
	}

	return localctx
}

// IPathContext is an interface to support dynamic dispatch.
type IPathContext interface {
	antlr.ParserRuleContext

	// GetParser returns the parser.
	GetParser() antlr.Parser

	// IsPathContext differentiates from other interfaces.
	IsPathContext()
}

type PathContext struct {
	*antlr.BaseParserRuleContext
	parser antlr.Parser
}

func NewEmptyPathContext() *PathContext {
	var p = new(PathContext)
	p.BaseParserRuleContext = antlr.NewBaseParserRuleContext(nil, -1)
	p.RuleIndex = AgeParserRULE_path
	return p
}

func (*PathContext) IsPathContext() {}

func NewPathContext(parser antlr.Parser, parent antlr.ParserRuleContext, invokingState int) *PathContext {
	var p = new(PathContext)

	p.BaseParserRuleContext = antlr.NewBaseParserRuleContext(parent, invokingState)

	p.parser = parser
	p.RuleIndex = AgeParserRULE_path

	return p
}

func (s *PathContext) GetParser() antlr.Parser { return s.parser }

func (s *PathContext) AllVertex() []IVertexContext {
	children := s.GetChildren()
	len := 0
	for _, ctx := range children {
		if _, ok := ctx.(IVertexContext); ok {
			len++
		}
	}

	tst := make([]IVertexContext, len)
	i := 0
	for _, ctx := range children {
		if t, ok := ctx.(IVertexContext); ok {
			tst[i] = t.(IVertexContext)
			i++
		}
	}

	return tst
}

func (s *PathContext) Vertex(i int) IVertexContext {
	var t antlr.RuleContext
	j := 0
	for _, ctx := range s.GetChildren() {
		if _, ok := ctx.(IVertexContext); ok {
			if j == i {
				t = ctx.(antlr.RuleContext)
				break
			}
			j++
		}
	}

	if t == nil {
		return nil
	}

	return t.(IVertexContext)
}

func (s *PathContext) KW_PATH() antlr.TerminalNode {
	return s.GetToken(AgeParserKW_PATH, 0)
}

func (s *PathContext) AllEdge() []IEdgeContext {
	children := s.GetChildren()
	len := 0
	for _, ctx := range children {
		if _, ok := ctx.(IEdgeContext); ok {
			len++
		}
	}

	tst := make([]IEdgeContext, len)
	i := 0
	for _, ctx := range children {
		if t, ok := ctx.(IEdgeContext); ok {
			tst[i] = t.(IEdgeContext)
			i++
		}
	}

	return tst
}

func (s *PathContext) Edge(i int) IEdgeContext {
	var t antlr.RuleContext
	j := 0
	for _, ctx := range s.GetChildren() {
		if _, ok := ctx.(IEdgeContext); ok {
			if j == i {
				t = ctx.(antlr.RuleContext)
				break
			}
			j++
		}
	}

	if t == nil {
		return nil
	}

	return t.(IEdgeContext)
}

func (s *PathContext) GetRuleContext() antlr.RuleContext {
	return s
}

func (s *PathContext) ToStringTree(ruleNames []string, recog antlr.Recognizer) string {
	return antlr.TreesStringTree(s, ruleNames, recog)
}

func (s *PathContext) EnterRule(listener antlr.ParseTreeListener) {
	if listenerT, ok := listener.(AgeListener); ok {
		listenerT.EnterPath(s)
	}
}

func (s *PathContext) ExitRule(listener antlr.ParseTreeListener) {
	if listenerT, ok := listener.(AgeListener); ok {
		listenerT.ExitPath(s)
	}
}

func (s *PathContext) Accept(visitor antlr.ParseTreeVisitor) interface{} {
	switch t := visitor.(type) {
	case AgeVisitor:
		return t.VisitPath(s)

	default:
		return t.VisitChildren(s)
	}
}

func (p *AgeParser) Path() (localctx IPathContext) {
	this := p
	_ = this

	localctx = NewPathContext(p, p.GetParserRuleContext(), p.GetState())
	p.EnterRule(localctx, 6, AgeParserRULE_path)
	var _la int

	defer func() {
		p.ExitRule()
	}()

	defer func() {
		if err := recover(); err != nil {
			if v, ok := err.(antlr.RecognitionException); ok {
				localctx.SetException(v)
				p.GetErrorHandler().ReportError(p, v)
				p.GetErrorHandler().Recover(p, v)
			} else {
				panic(err)
			}
		}
	}()

	p.EnterOuterAlt(localctx, 1)
	{
		p.SetState(28)
		p.Match(AgeParserT__0)
	}
	{
		p.SetState(29)
		p.Vertex()
	}
	p.SetState(37)
	p.GetErrorHandler().Sync(p)
	_la = p.GetTokenStream().LA(1)

	for _la == AgeParserT__1 {
		{
			p.SetState(30)
			p.Match(AgeParserT__1)
		}
		{
			p.SetState(31)
			p.Edge()
		}
		{
			p.SetState(32)
			p.Match(AgeParserT__1)
		}
		{
			p.SetState(33)
			p.Vertex()
		}

		p.SetState(39)
		p.GetErrorHandler().Sync(p)
		_la = p.GetTokenStream().LA(1)
	}
	{
		p.SetState(40)
		p.Match(AgeParserT__2)
	}
	{
		p.SetState(41)
		p.Match(AgeParserKW_PATH)
	}

	return localctx
}

// IValueContext is an interface to support dynamic dispatch.
type IValueContext interface {
	antlr.ParserRuleContext

	// GetParser returns the parser.
	GetParser() antlr.Parser

	// IsValueContext differentiates from other interfaces.
	IsValueContext()
}

type ValueContext struct {
	*antlr.BaseParserRuleContext
	parser antlr.Parser
}

func NewEmptyValueContext() *ValueContext {
	var p = new(ValueContext)
	p.BaseParserRuleContext = antlr.NewBaseParserRuleContext(nil, -1)
	p.RuleIndex = AgeParserRULE_value
	return p
}

func (*ValueContext) IsValueContext() {}

func NewValueContext(parser antlr.Parser, parent antlr.ParserRuleContext, invokingState int) *ValueContext {
	var p = new(ValueContext)

	p.BaseParserRuleContext = antlr.NewBaseParserRuleContext(parent, invokingState)

	p.parser = parser
	p.RuleIndex = AgeParserRULE_value

	return p
}

func (s *ValueContext) GetParser() antlr.Parser { return s.parser }

func (s *ValueContext) STRING() antlr.TerminalNode {
	return s.GetToken(AgeParserSTRING, 0)
}

func (s *ValueContext) NUMBER() antlr.TerminalNode {
	return s.GetToken(AgeParserNUMBER, 0)
}

func (s *ValueContext) NUMERIC() antlr.TerminalNode {
	return s.GetToken(AgeParserNUMERIC, 0)
}

func (s *ValueContext) FLOAT_EXPR() antlr.TerminalNode {
	return s.GetToken(AgeParserFLOAT_EXPR, 0)
}

func (s *ValueContext) BOOL() antlr.TerminalNode {
	return s.GetToken(AgeParserBOOL, 0)
}

func (s *ValueContext) NULL() antlr.TerminalNode {
	return s.GetToken(AgeParserNULL, 0)
}

func (s *ValueContext) Properties() IPropertiesContext {
	var t antlr.RuleContext
	for _, ctx := range s.GetChildren() {
		if _, ok := ctx.(IPropertiesContext); ok {
			t = ctx.(antlr.RuleContext)
			break
		}
	}

	if t == nil {
		return nil
	}

	return t.(IPropertiesContext)
}

func (s *ValueContext) Arr() IArrContext {
	var t antlr.RuleContext
	for _, ctx := range s.GetChildren() {
		if _, ok := ctx.(IArrContext); ok {
			t = ctx.(antlr.RuleContext)
			break
		}
	}

	if t == nil {
		return nil
	}

	return t.(IArrContext)
}

func (s *ValueContext) GetRuleContext() antlr.RuleContext {
	return s
}

func (s *ValueContext) ToStringTree(ruleNames []string, recog antlr.Recognizer) string {
	return antlr.TreesStringTree(s, ruleNames, recog)
}

func (s *ValueContext) EnterRule(listener antlr.ParseTreeListener) {
	if listenerT, ok := listener.(AgeListener); ok {
		listenerT.EnterValue(s)
	}
}

func (s *ValueContext) ExitRule(listener antlr.ParseTreeListener) {
	if listenerT, ok := listener.(AgeListener); ok {
		listenerT.ExitValue(s)
	}
}

func (s *ValueContext) Accept(visitor antlr.ParseTreeVisitor) interface{} {
	switch t := visitor.(type) {
	case AgeVisitor:
		return t.VisitValue(s)

	default:
		return t.VisitChildren(s)
	}
}

func (p *AgeParser) Value() (localctx IValueContext) {
	this := p
	_ = this

	localctx = NewValueContext(p, p.GetParserRuleContext(), p.GetState())
	p.EnterRule(localctx, 8, AgeParserRULE_value)

	defer func() {
		p.ExitRule()
	}()

	defer func() {
		if err := recover(); err != nil {
			if v, ok := err.(antlr.RecognitionException); ok {
				localctx.SetException(v)
				p.GetErrorHandler().ReportError(p, v)
				p.GetErrorHandler().Recover(p, v)
			} else {
				panic(err)
			}
		}
	}()

	p.SetState(51)
	p.GetErrorHandler().Sync(p)

	switch p.GetTokenStream().LA(1) {
	case AgeParserSTRING:
		p.EnterOuterAlt(localctx, 1)
		{
			p.SetState(43)
			p.Match(AgeParserSTRING)
		}

	case AgeParserNUMBER:
		p.EnterOuterAlt(localctx, 2)
		{
			p.SetState(44)
			p.Match(AgeParserNUMBER)
		}

	case AgeParserNUMERIC:
		p.EnterOuterAlt(localctx, 3)
		{
			p.SetState(45)
			p.Match(AgeParserNUMERIC)
		}

	case AgeParserFLOAT_EXPR:
		p.EnterOuterAlt(localctx, 4)
		{
			p.SetState(46)
			p.Match(AgeParserFLOAT_EXPR)
		}

	case AgeParserBOOL:
		p.EnterOuterAlt(localctx, 5)
		{
			p.SetState(47)
			p.Match(AgeParserBOOL)
		}

	case AgeParserNULL:
		p.EnterOuterAlt(localctx, 6)
		{
			p.SetState(48)
			p.Match(AgeParserNULL)
		}

	case AgeParserT__3:
		p.EnterOuterAlt(localctx, 7)
		{
			p.SetState(49)
			p.Properties()
		}

	case AgeParserT__0:
		p.EnterOuterAlt(localctx, 8)
		{
			p.SetState(50)
			p.Arr()
		}

	default:
		panic(antlr.NewNoViableAltException(p, nil, nil, nil, nil, nil))
	}

	return localctx
}

// IPropertiesContext is an interface to support dynamic dispatch.
type IPropertiesContext interface {
	antlr.ParserRuleContext

	// GetParser returns the parser.
	GetParser() antlr.Parser

	// IsPropertiesContext differentiates from other interfaces.
	IsPropertiesContext()
}

type PropertiesContext struct {
	*antlr.BaseParserRuleContext
	parser antlr.Parser
}

func NewEmptyPropertiesContext() *PropertiesContext {
	var p = new(PropertiesContext)
	p.BaseParserRuleContext = antlr.NewBaseParserRuleContext(nil, -1)
	p.RuleIndex = AgeParserRULE_properties
	return p
}

func (*PropertiesContext) IsPropertiesContext() {}

func NewPropertiesContext(parser antlr.Parser, parent antlr.ParserRuleContext, invokingState int) *PropertiesContext {
	var p = new(PropertiesContext)

	p.BaseParserRuleContext = antlr.NewBaseParserRuleContext(parent, invokingState)

	p.parser = parser
	p.RuleIndex = AgeParserRULE_properties

	return p
}

func (s *PropertiesContext) GetParser() antlr.Parser { return s.parser }

func (s *PropertiesContext) AllPair() []IPairContext {
	children := s.GetChildren()
	len := 0
	for _, ctx := range children {
		if _, ok := ctx.(IPairContext); ok {
			len++
		}
	}

	tst := make([]IPairContext, len)
	i := 0
	for _, ctx := range children {
		if t, ok := ctx.(IPairContext); ok {
			tst[i] = t.(IPairContext)
			i++
		}
	}

	return tst
}

func (s *PropertiesContext) Pair(i int) IPairContext {
	var t antlr.RuleContext
	j := 0
	for _, ctx := range s.GetChildren() {
		if _, ok := ctx.(IPairContext); ok {
			if j == i {
				t = ctx.(antlr.RuleContext)
				break
			}
			j++
		}
	}

	if t == nil {
		return nil
	}

	return t.(IPairContext)
}

func (s *PropertiesContext) GetRuleContext() antlr.RuleContext {
	return s
}

func (s *PropertiesContext) ToStringTree(ruleNames []string, recog antlr.Recognizer) string {
	return antlr.TreesStringTree(s, ruleNames, recog)
}

func (s *PropertiesContext) EnterRule(listener antlr.ParseTreeListener) {
	if listenerT, ok := listener.(AgeListener); ok {
		listenerT.EnterProperties(s)
	}
}

func (s *PropertiesContext) ExitRule(listener antlr.ParseTreeListener) {
	if listenerT, ok := listener.(AgeListener); ok {
		listenerT.ExitProperties(s)
	}
}

func (s *PropertiesContext) Accept(visitor antlr.ParseTreeVisitor) interface{} {
	switch t := visitor.(type) {
	case AgeVisitor:
		return t.VisitProperties(s)

	default:
		return t.VisitChildren(s)
	}
}

func (p *AgeParser) Properties() (localctx IPropertiesContext) {
	this := p
	_ = this

	localctx = NewPropertiesContext(p, p.GetParserRuleContext(), p.GetState())
	p.EnterRule(localctx, 10, AgeParserRULE_properties)
	var _la int

	defer func() {
		p.ExitRule()
	}()

	defer func() {
		if err := recover(); err != nil {
			if v, ok := err.(antlr.RecognitionException); ok {
				localctx.SetException(v)
				p.GetErrorHandler().ReportError(p, v)
				p.GetErrorHandler().Recover(p, v)
			} else {
				panic(err)
			}
		}
	}()

	p.SetState(66)
	p.GetErrorHandler().Sync(p)
	switch p.GetInterpreter().AdaptivePredict(p.GetTokenStream(), 4, p.GetParserRuleContext()) {
	case 1:
		p.EnterOuterAlt(localctx, 1)
		{
			p.SetState(53)
			p.Match(AgeParserT__3)
		}
		{
			p.SetState(54)
			p.Pair()
		}
		p.SetState(59)
		p.GetErrorHandler().Sync(p)
		_la = p.GetTokenStream().LA(1)

		for _la == AgeParserT__1 {
			{
				p.SetState(55)
				p.Match(AgeParserT__1)
			}
			{
				p.SetState(56)
				p.Pair()
			}

			p.SetState(61)
			p.GetErrorHandler().Sync(p)
			_la = p.GetTokenStream().LA(1)
		}
		{
			p.SetState(62)
			p.Match(AgeParserT__4)
		}

	case 2:
		p.EnterOuterAlt(localctx, 2)
		{
			p.SetState(64)
			p.Match(AgeParserT__3)
		}
		{
			p.SetState(65)
			p.Match(AgeParserT__4)
		}

	}

	return localctx
}

// IPairContext is an interface to support dynamic dispatch.
type IPairContext interface {
	antlr.ParserRuleContext

	// GetParser returns the parser.
	GetParser() antlr.Parser

	// IsPairContext differentiates from other interfaces.
	IsPairContext()
}

type PairContext struct {
	*antlr.BaseParserRuleContext
	parser antlr.Parser
}

func NewEmptyPairContext() *PairContext {
	var p = new(PairContext)
	p.BaseParserRuleContext = antlr.NewBaseParserRuleContext(nil, -1)
	p.RuleIndex = AgeParserRULE_pair
	return p
}

func (*PairContext) IsPairContext() {}

func NewPairContext(parser antlr.Parser, parent antlr.ParserRuleContext, invokingState int) *PairContext {
	var p = new(PairContext)

	p.BaseParserRuleContext = antlr.NewBaseParserRuleContext(parent, invokingState)

	p.parser = parser
	p.RuleIndex = AgeParserRULE_pair

	return p
}

func (s *PairContext) GetParser() antlr.Parser { return s.parser }

func (s *PairContext) STRING() antlr.TerminalNode {
	return s.GetToken(AgeParserSTRING, 0)
}

func (s *PairContext) Value() IValueContext {
	var t antlr.RuleContext
	for _, ctx := range s.GetChildren() {
		if _, ok := ctx.(IValueContext); ok {
			t = ctx.(antlr.RuleContext)
			break
		}
	}

	if t == nil {
		return nil
	}

	return t.(IValueContext)
}

func (s *PairContext) GetRuleContext() antlr.RuleContext {
	return s
}

func (s *PairContext) ToStringTree(ruleNames []string, recog antlr.Recognizer) string {
	return antlr.TreesStringTree(s, ruleNames, recog)
}

func (s *PairContext) EnterRule(listener antlr.ParseTreeListener) {
	if listenerT, ok := listener.(AgeListener); ok {
		listenerT.EnterPair(s)
	}
}

func (s *PairContext) ExitRule(listener antlr.ParseTreeListener) {
	if listenerT, ok := listener.(AgeListener); ok {
		listenerT.ExitPair(s)
	}
}

func (s *PairContext) Accept(visitor antlr.ParseTreeVisitor) interface{} {
	switch t := visitor.(type) {
	case AgeVisitor:
		return t.VisitPair(s)

	default:
		return t.VisitChildren(s)
	}
}

func (p *AgeParser) Pair() (localctx IPairContext) {
	this := p
	_ = this

	localctx = NewPairContext(p, p.GetParserRuleContext(), p.GetState())
	p.EnterRule(localctx, 12, AgeParserRULE_pair)

	defer func() {
		p.ExitRule()
	}()

	defer func() {
		if err := recover(); err != nil {
			if v, ok := err.(antlr.RecognitionException); ok {
				localctx.SetException(v)
				p.GetErrorHandler().ReportError(p, v)
				p.GetErrorHandler().Recover(p, v)
			} else {
				panic(err)
			}
		}
	}()

	p.EnterOuterAlt(localctx, 1)
	{
		p.SetState(68)
		p.Match(AgeParserSTRING)
	}
	{
		p.SetState(69)
		p.Match(AgeParserT__5)
	}
	{
		p.SetState(70)
		p.Value()
	}

	return localctx
}

// IArrContext is an interface to support dynamic dispatch.
type IArrContext interface {
	antlr.ParserRuleContext

	// GetParser returns the parser.
	GetParser() antlr.Parser

	// IsArrContext differentiates from other interfaces.
	IsArrContext()
}

type ArrContext struct {
	*antlr.BaseParserRuleContext
	parser antlr.Parser
}

func NewEmptyArrContext() *ArrContext {
	var p = new(ArrContext)
	p.BaseParserRuleContext = antlr.NewBaseParserRuleContext(nil, -1)
	p.RuleIndex = AgeParserRULE_arr
	return p
}

func (*ArrContext) IsArrContext() {}

func NewArrContext(parser antlr.Parser, parent antlr.ParserRuleContext, invokingState int) *ArrContext {
	var p = new(ArrContext)

	p.BaseParserRuleContext = antlr.NewBaseParserRuleContext(parent, invokingState)

	p.parser = parser
	p.RuleIndex = AgeParserRULE_arr

	return p
}

func (s *ArrContext) GetParser() antlr.Parser { return s.parser }

func (s *ArrContext) AllValue() []IValueContext {
	children := s.GetChildren()
	len := 0
	for _, ctx := range children {
		if _, ok := ctx.(IValueContext); ok {
			len++
		}
	}

	tst := make([]IValueContext, len)
	i := 0
	for _, ctx := range children {
		if t, ok := ctx.(IValueContext); ok {
			tst[i] = t.(IValueContext)
			i++
		}
	}

	return tst
}

func (s *ArrContext) Value(i int) IValueContext {
	var t antlr.RuleContext
	j := 0
	for _, ctx := range s.GetChildren() {
		if _, ok := ctx.(IValueContext); ok {
			if j == i {
				t = ctx.(antlr.RuleContext)
				break
			}
			j++
		}
	}

	if t == nil {
		return nil
	}

	return t.(IValueContext)
}

func (s *ArrContext) GetRuleContext() antlr.RuleContext {
	return s
}

func (s *ArrContext) ToStringTree(ruleNames []string, recog antlr.Recognizer) string {
	return antlr.TreesStringTree(s, ruleNames, recog)
}

func (s *ArrContext) EnterRule(listener antlr.ParseTreeListener) {
	if listenerT, ok := listener.(AgeListener); ok {
		listenerT.EnterArr(s)
	}
}

func (s *ArrContext) ExitRule(listener antlr.ParseTreeListener) {
	if listenerT, ok := listener.(AgeListener); ok {
		listenerT.ExitArr(s)
	}
}

func (s *ArrContext) Accept(visitor antlr.ParseTreeVisitor) interface{} {
	switch t := visitor.(type) {
	case AgeVisitor:
		return t.VisitArr(s)

	default:
		return t.VisitChildren(s)
	}
}

func (p *AgeParser) Arr() (localctx IArrContext) {
	this := p
	_ = this

	localctx = NewArrContext(p, p.GetParserRuleContext(), p.GetState())
	p.EnterRule(localctx, 14, AgeParserRULE_arr)
	var _la int

	defer func() {
		p.ExitRule()
	}()

	defer func() {
		if err := recover(); err != nil {
			if v, ok := err.(antlr.RecognitionException); ok {
				localctx.SetException(v)
				p.GetErrorHandler().ReportError(p, v)
				p.GetErrorHandler().Recover(p, v)
			} else {
				panic(err)
			}
		}
	}()

	p.SetState(85)
	p.GetErrorHandler().Sync(p)
	switch p.GetInterpreter().AdaptivePredict(p.GetTokenStream(), 6, p.GetParserRuleContext()) {
	case 1:
		p.EnterOuterAlt(localctx, 1)
		{
			p.SetState(72)
			p.Match(AgeParserT__0)
		}
		{
			p.SetState(73)
			p.Value()
		}
		p.SetState(78)
		p.GetErrorHandler().Sync(p)
		_la = p.GetTokenStream().LA(1)

		for _la == AgeParserT__1 {
			{
				p.SetState(74)
				p.Match(AgeParserT__1)
			}
			{
				p.SetState(75)
				p.Value()
			}

			p.SetState(80)
			p.GetErrorHandler().Sync(p)
			_la = p.GetTokenStream().LA(1)
		}
		{
			p.SetState(81)
			p.Match(AgeParserT__2)
		}

	case 2:
		p.EnterOuterAlt(localctx, 2)
		{
			p.SetState(83)
			p.Match(AgeParserT__0)
		}
		{
			p.SetState(84)
			p.Match(AgeParserT__2)
		}

	}

	return localctx
}
