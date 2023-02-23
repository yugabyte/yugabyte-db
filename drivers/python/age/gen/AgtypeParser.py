# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
# http://www.apache.org/licenses/LICENSE-2.0
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
# Generated from ../Agtype.g4 by ANTLR 4.11.1
# encoding: utf-8

from antlr4 import *
from io import StringIO
import sys
if sys.version_info[1] > 5:
	from typing import TextIO
else:
	from typing.io import TextIO

def serializedATN():
    return [
        4,1,19,80,2,0,7,0,2,1,7,1,2,2,7,2,2,3,7,3,2,4,7,4,2,5,7,5,2,6,7,
        6,2,7,7,7,1,0,1,0,1,0,1,1,1,1,3,1,22,8,1,1,2,1,2,1,2,1,2,1,2,1,2,
        1,2,1,2,3,2,32,8,2,1,3,1,3,1,3,1,3,5,3,38,8,3,10,3,12,3,41,9,3,1,
        3,1,3,1,3,1,3,3,3,47,8,3,1,4,1,4,1,4,1,4,1,5,1,5,1,5,1,5,5,5,57,
        8,5,10,5,12,5,60,9,5,1,5,1,5,1,5,1,5,3,5,66,8,5,1,6,1,6,1,6,1,7,
        1,7,1,7,3,7,74,8,7,1,7,1,7,3,7,78,8,7,1,7,0,0,8,0,2,4,6,8,10,12,
        14,0,0,87,0,16,1,0,0,0,2,19,1,0,0,0,4,31,1,0,0,0,6,46,1,0,0,0,8,
        48,1,0,0,0,10,65,1,0,0,0,12,67,1,0,0,0,14,77,1,0,0,0,16,17,3,2,1,
        0,17,18,5,0,0,1,18,1,1,0,0,0,19,21,3,4,2,0,20,22,3,12,6,0,21,20,
        1,0,0,0,21,22,1,0,0,0,22,3,1,0,0,0,23,32,5,15,0,0,24,32,5,16,0,0,
        25,32,3,14,7,0,26,32,5,1,0,0,27,32,5,2,0,0,28,32,5,3,0,0,29,32,3,
        6,3,0,30,32,3,10,5,0,31,23,1,0,0,0,31,24,1,0,0,0,31,25,1,0,0,0,31,
        26,1,0,0,0,31,27,1,0,0,0,31,28,1,0,0,0,31,29,1,0,0,0,31,30,1,0,0,
        0,32,5,1,0,0,0,33,34,5,4,0,0,34,39,3,8,4,0,35,36,5,5,0,0,36,38,3,
        8,4,0,37,35,1,0,0,0,38,41,1,0,0,0,39,37,1,0,0,0,39,40,1,0,0,0,40,
        42,1,0,0,0,41,39,1,0,0,0,42,43,5,6,0,0,43,47,1,0,0,0,44,45,5,4,0,
        0,45,47,5,6,0,0,46,33,1,0,0,0,46,44,1,0,0,0,47,7,1,0,0,0,48,49,5,
        15,0,0,49,50,5,7,0,0,50,51,3,2,1,0,51,9,1,0,0,0,52,53,5,8,0,0,53,
        58,3,2,1,0,54,55,5,5,0,0,55,57,3,2,1,0,56,54,1,0,0,0,57,60,1,0,0,
        0,58,56,1,0,0,0,58,59,1,0,0,0,59,61,1,0,0,0,60,58,1,0,0,0,61,62,
        5,9,0,0,62,66,1,0,0,0,63,64,5,8,0,0,64,66,5,9,0,0,65,52,1,0,0,0,
        65,63,1,0,0,0,66,11,1,0,0,0,67,68,5,10,0,0,68,69,5,14,0,0,69,13,
        1,0,0,0,70,78,5,17,0,0,71,78,5,18,0,0,72,74,5,11,0,0,73,72,1,0,0,
        0,73,74,1,0,0,0,74,75,1,0,0,0,75,78,5,12,0,0,76,78,5,13,0,0,77,70,
        1,0,0,0,77,71,1,0,0,0,77,73,1,0,0,0,77,76,1,0,0,0,78,15,1,0,0,0,
        8,21,31,39,46,58,65,73,77
    ]

class AgtypeParser ( Parser ):

    grammarFileName = "Agtype.g4"

    atn = ATNDeserializer().deserialize(serializedATN())

    decisionsToDFA = [ DFA(ds, i) for i, ds in enumerate(atn.decisionToState) ]

    sharedContextCache = PredictionContextCache()

    literalNames = [ "<INVALID>", "'true'", "'false'", "'null'", "'{'", 
                     "','", "'}'", "':'", "'['", "']'", "'::'", "'-'", "'Infinity'", 
                     "'NaN'" ]

    symbolicNames = [ "<INVALID>", "<INVALID>", "<INVALID>", "<INVALID>", 
                      "<INVALID>", "<INVALID>", "<INVALID>", "<INVALID>", 
                      "<INVALID>", "<INVALID>", "<INVALID>", "<INVALID>", 
                      "<INVALID>", "<INVALID>", "IDENT", "STRING", "INTEGER", 
                      "RegularFloat", "ExponentFloat", "WS" ]

    RULE_agType = 0
    RULE_agValue = 1
    RULE_value = 2
    RULE_obj = 3
    RULE_pair = 4
    RULE_array = 5
    RULE_typeAnnotation = 6
    RULE_floatLiteral = 7

    ruleNames =  [ "agType", "agValue", "value", "obj", "pair", "array", 
                   "typeAnnotation", "floatLiteral" ]

    EOF = Token.EOF
    T__0=1
    T__1=2
    T__2=3
    T__3=4
    T__4=5
    T__5=6
    T__6=7
    T__7=8
    T__8=9
    T__9=10
    T__10=11
    T__11=12
    T__12=13
    IDENT=14
    STRING=15
    INTEGER=16
    RegularFloat=17
    ExponentFloat=18
    WS=19

    def __init__(self, input:TokenStream, output:TextIO = sys.stdout):
        super().__init__(input, output)
        self.checkVersion("4.11.1")
        self._interp = ParserATNSimulator(self, self.atn, self.decisionsToDFA, self.sharedContextCache)
        self._predicates = None




    class AgTypeContext(ParserRuleContext):
        __slots__ = 'parser'

        def __init__(self, parser, parent:ParserRuleContext=None, invokingState:int=-1):
            super().__init__(parent, invokingState)
            self.parser = parser

        def agValue(self):
            return self.getTypedRuleContext(AgtypeParser.AgValueContext,0)


        def EOF(self):
            return self.getToken(AgtypeParser.EOF, 0)

        def getRuleIndex(self):
            return AgtypeParser.RULE_agType

        def enterRule(self, listener:ParseTreeListener):
            if hasattr( listener, "enterAgType" ):
                listener.enterAgType(self)

        def exitRule(self, listener:ParseTreeListener):
            if hasattr( listener, "exitAgType" ):
                listener.exitAgType(self)

        def accept(self, visitor:ParseTreeVisitor):
            if hasattr( visitor, "visitAgType" ):
                return visitor.visitAgType(self)
            else:
                return visitor.visitChildren(self)




    def agType(self):

        localctx = AgtypeParser.AgTypeContext(self, self._ctx, self.state)
        self.enterRule(localctx, 0, self.RULE_agType)
        try:
            self.enterOuterAlt(localctx, 1)
            self.state = 16
            self.agValue()
            self.state = 17
            self.match(AgtypeParser.EOF)
        except RecognitionException as re:
            localctx.exception = re
            self._errHandler.reportError(self, re)
            self._errHandler.recover(self, re)
        finally:
            self.exitRule()
        return localctx


    class AgValueContext(ParserRuleContext):
        __slots__ = 'parser'

        def __init__(self, parser, parent:ParserRuleContext=None, invokingState:int=-1):
            super().__init__(parent, invokingState)
            self.parser = parser

        def value(self):
            return self.getTypedRuleContext(AgtypeParser.ValueContext,0)


        def typeAnnotation(self):
            return self.getTypedRuleContext(AgtypeParser.TypeAnnotationContext,0)


        def getRuleIndex(self):
            return AgtypeParser.RULE_agValue

        def enterRule(self, listener:ParseTreeListener):
            if hasattr( listener, "enterAgValue" ):
                listener.enterAgValue(self)

        def exitRule(self, listener:ParseTreeListener):
            if hasattr( listener, "exitAgValue" ):
                listener.exitAgValue(self)

        def accept(self, visitor:ParseTreeVisitor):
            if hasattr( visitor, "visitAgValue" ):
                return visitor.visitAgValue(self)
            else:
                return visitor.visitChildren(self)




    def agValue(self):

        localctx = AgtypeParser.AgValueContext(self, self._ctx, self.state)
        self.enterRule(localctx, 2, self.RULE_agValue)
        self._la = 0 # Token type
        try:
            self.enterOuterAlt(localctx, 1)
            self.state = 19
            self.value()
            self.state = 21
            self._errHandler.sync(self)
            _la = self._input.LA(1)
            if _la==10:
                self.state = 20
                self.typeAnnotation()


        except RecognitionException as re:
            localctx.exception = re
            self._errHandler.reportError(self, re)
            self._errHandler.recover(self, re)
        finally:
            self.exitRule()
        return localctx


    class ValueContext(ParserRuleContext):
        __slots__ = 'parser'

        def __init__(self, parser, parent:ParserRuleContext=None, invokingState:int=-1):
            super().__init__(parent, invokingState)
            self.parser = parser


        def getRuleIndex(self):
            return AgtypeParser.RULE_value

     
        def copyFrom(self, ctx:ParserRuleContext):
            super().copyFrom(ctx)



    class NullValueContext(ValueContext):

        def __init__(self, parser, ctx:ParserRuleContext): # actually a AgtypeParser.ValueContext
            super().__init__(parser)
            self.copyFrom(ctx)


        def enterRule(self, listener:ParseTreeListener):
            if hasattr( listener, "enterNullValue" ):
                listener.enterNullValue(self)

        def exitRule(self, listener:ParseTreeListener):
            if hasattr( listener, "exitNullValue" ):
                listener.exitNullValue(self)

        def accept(self, visitor:ParseTreeVisitor):
            if hasattr( visitor, "visitNullValue" ):
                return visitor.visitNullValue(self)
            else:
                return visitor.visitChildren(self)


    class ObjectValueContext(ValueContext):

        def __init__(self, parser, ctx:ParserRuleContext): # actually a AgtypeParser.ValueContext
            super().__init__(parser)
            self.copyFrom(ctx)

        def obj(self):
            return self.getTypedRuleContext(AgtypeParser.ObjContext,0)


        def enterRule(self, listener:ParseTreeListener):
            if hasattr( listener, "enterObjectValue" ):
                listener.enterObjectValue(self)

        def exitRule(self, listener:ParseTreeListener):
            if hasattr( listener, "exitObjectValue" ):
                listener.exitObjectValue(self)

        def accept(self, visitor:ParseTreeVisitor):
            if hasattr( visitor, "visitObjectValue" ):
                return visitor.visitObjectValue(self)
            else:
                return visitor.visitChildren(self)


    class IntegerValueContext(ValueContext):

        def __init__(self, parser, ctx:ParserRuleContext): # actually a AgtypeParser.ValueContext
            super().__init__(parser)
            self.copyFrom(ctx)

        def INTEGER(self):
            return self.getToken(AgtypeParser.INTEGER, 0)

        def enterRule(self, listener:ParseTreeListener):
            if hasattr( listener, "enterIntegerValue" ):
                listener.enterIntegerValue(self)

        def exitRule(self, listener:ParseTreeListener):
            if hasattr( listener, "exitIntegerValue" ):
                listener.exitIntegerValue(self)

        def accept(self, visitor:ParseTreeVisitor):
            if hasattr( visitor, "visitIntegerValue" ):
                return visitor.visitIntegerValue(self)
            else:
                return visitor.visitChildren(self)


    class TrueBooleanContext(ValueContext):

        def __init__(self, parser, ctx:ParserRuleContext): # actually a AgtypeParser.ValueContext
            super().__init__(parser)
            self.copyFrom(ctx)


        def enterRule(self, listener:ParseTreeListener):
            if hasattr( listener, "enterTrueBoolean" ):
                listener.enterTrueBoolean(self)

        def exitRule(self, listener:ParseTreeListener):
            if hasattr( listener, "exitTrueBoolean" ):
                listener.exitTrueBoolean(self)

        def accept(self, visitor:ParseTreeVisitor):
            if hasattr( visitor, "visitTrueBoolean" ):
                return visitor.visitTrueBoolean(self)
            else:
                return visitor.visitChildren(self)


    class FalseBooleanContext(ValueContext):

        def __init__(self, parser, ctx:ParserRuleContext): # actually a AgtypeParser.ValueContext
            super().__init__(parser)
            self.copyFrom(ctx)


        def enterRule(self, listener:ParseTreeListener):
            if hasattr( listener, "enterFalseBoolean" ):
                listener.enterFalseBoolean(self)

        def exitRule(self, listener:ParseTreeListener):
            if hasattr( listener, "exitFalseBoolean" ):
                listener.exitFalseBoolean(self)

        def accept(self, visitor:ParseTreeVisitor):
            if hasattr( visitor, "visitFalseBoolean" ):
                return visitor.visitFalseBoolean(self)
            else:
                return visitor.visitChildren(self)


    class FloatValueContext(ValueContext):

        def __init__(self, parser, ctx:ParserRuleContext): # actually a AgtypeParser.ValueContext
            super().__init__(parser)
            self.copyFrom(ctx)

        def floatLiteral(self):
            return self.getTypedRuleContext(AgtypeParser.FloatLiteralContext,0)


        def enterRule(self, listener:ParseTreeListener):
            if hasattr( listener, "enterFloatValue" ):
                listener.enterFloatValue(self)

        def exitRule(self, listener:ParseTreeListener):
            if hasattr( listener, "exitFloatValue" ):
                listener.exitFloatValue(self)

        def accept(self, visitor:ParseTreeVisitor):
            if hasattr( visitor, "visitFloatValue" ):
                return visitor.visitFloatValue(self)
            else:
                return visitor.visitChildren(self)


    class StringValueContext(ValueContext):

        def __init__(self, parser, ctx:ParserRuleContext): # actually a AgtypeParser.ValueContext
            super().__init__(parser)
            self.copyFrom(ctx)

        def STRING(self):
            return self.getToken(AgtypeParser.STRING, 0)

        def enterRule(self, listener:ParseTreeListener):
            if hasattr( listener, "enterStringValue" ):
                listener.enterStringValue(self)

        def exitRule(self, listener:ParseTreeListener):
            if hasattr( listener, "exitStringValue" ):
                listener.exitStringValue(self)

        def accept(self, visitor:ParseTreeVisitor):
            if hasattr( visitor, "visitStringValue" ):
                return visitor.visitStringValue(self)
            else:
                return visitor.visitChildren(self)


    class ArrayValueContext(ValueContext):

        def __init__(self, parser, ctx:ParserRuleContext): # actually a AgtypeParser.ValueContext
            super().__init__(parser)
            self.copyFrom(ctx)

        def array(self):
            return self.getTypedRuleContext(AgtypeParser.ArrayContext,0)


        def enterRule(self, listener:ParseTreeListener):
            if hasattr( listener, "enterArrayValue" ):
                listener.enterArrayValue(self)

        def exitRule(self, listener:ParseTreeListener):
            if hasattr( listener, "exitArrayValue" ):
                listener.exitArrayValue(self)

        def accept(self, visitor:ParseTreeVisitor):
            if hasattr( visitor, "visitArrayValue" ):
                return visitor.visitArrayValue(self)
            else:
                return visitor.visitChildren(self)



    def value(self):

        localctx = AgtypeParser.ValueContext(self, self._ctx, self.state)
        self.enterRule(localctx, 4, self.RULE_value)
        try:
            self.state = 31
            self._errHandler.sync(self)
            token = self._input.LA(1)
            if token in [15]:
                localctx = AgtypeParser.StringValueContext(self, localctx)
                self.enterOuterAlt(localctx, 1)
                self.state = 23
                self.match(AgtypeParser.STRING)
                pass
            elif token in [16]:
                localctx = AgtypeParser.IntegerValueContext(self, localctx)
                self.enterOuterAlt(localctx, 2)
                self.state = 24
                self.match(AgtypeParser.INTEGER)
                pass
            elif token in [11, 12, 13, 17, 18]:
                localctx = AgtypeParser.FloatValueContext(self, localctx)
                self.enterOuterAlt(localctx, 3)
                self.state = 25
                self.floatLiteral()
                pass
            elif token in [1]:
                localctx = AgtypeParser.TrueBooleanContext(self, localctx)
                self.enterOuterAlt(localctx, 4)
                self.state = 26
                self.match(AgtypeParser.T__0)
                pass
            elif token in [2]:
                localctx = AgtypeParser.FalseBooleanContext(self, localctx)
                self.enterOuterAlt(localctx, 5)
                self.state = 27
                self.match(AgtypeParser.T__1)
                pass
            elif token in [3]:
                localctx = AgtypeParser.NullValueContext(self, localctx)
                self.enterOuterAlt(localctx, 6)
                self.state = 28
                self.match(AgtypeParser.T__2)
                pass
            elif token in [4]:
                localctx = AgtypeParser.ObjectValueContext(self, localctx)
                self.enterOuterAlt(localctx, 7)
                self.state = 29
                self.obj()
                pass
            elif token in [8]:
                localctx = AgtypeParser.ArrayValueContext(self, localctx)
                self.enterOuterAlt(localctx, 8)
                self.state = 30
                self.array()
                pass
            else:
                raise NoViableAltException(self)

        except RecognitionException as re:
            localctx.exception = re
            self._errHandler.reportError(self, re)
            self._errHandler.recover(self, re)
        finally:
            self.exitRule()
        return localctx


    class ObjContext(ParserRuleContext):
        __slots__ = 'parser'

        def __init__(self, parser, parent:ParserRuleContext=None, invokingState:int=-1):
            super().__init__(parent, invokingState)
            self.parser = parser

        def pair(self, i:int=None):
            if i is None:
                return self.getTypedRuleContexts(AgtypeParser.PairContext)
            else:
                return self.getTypedRuleContext(AgtypeParser.PairContext,i)


        def getRuleIndex(self):
            return AgtypeParser.RULE_obj

        def enterRule(self, listener:ParseTreeListener):
            if hasattr( listener, "enterObj" ):
                listener.enterObj(self)

        def exitRule(self, listener:ParseTreeListener):
            if hasattr( listener, "exitObj" ):
                listener.exitObj(self)

        def accept(self, visitor:ParseTreeVisitor):
            if hasattr( visitor, "visitObj" ):
                return visitor.visitObj(self)
            else:
                return visitor.visitChildren(self)




    def obj(self):

        localctx = AgtypeParser.ObjContext(self, self._ctx, self.state)
        self.enterRule(localctx, 6, self.RULE_obj)
        self._la = 0 # Token type
        try:
            self.state = 46
            self._errHandler.sync(self)
            la_ = self._interp.adaptivePredict(self._input,3,self._ctx)
            if la_ == 1:
                self.enterOuterAlt(localctx, 1)
                self.state = 33
                self.match(AgtypeParser.T__3)
                self.state = 34
                self.pair()
                self.state = 39
                self._errHandler.sync(self)
                _la = self._input.LA(1)
                while _la==5:
                    self.state = 35
                    self.match(AgtypeParser.T__4)
                    self.state = 36
                    self.pair()
                    self.state = 41
                    self._errHandler.sync(self)
                    _la = self._input.LA(1)

                self.state = 42
                self.match(AgtypeParser.T__5)
                pass

            elif la_ == 2:
                self.enterOuterAlt(localctx, 2)
                self.state = 44
                self.match(AgtypeParser.T__3)
                self.state = 45
                self.match(AgtypeParser.T__5)
                pass


        except RecognitionException as re:
            localctx.exception = re
            self._errHandler.reportError(self, re)
            self._errHandler.recover(self, re)
        finally:
            self.exitRule()
        return localctx


    class PairContext(ParserRuleContext):
        __slots__ = 'parser'

        def __init__(self, parser, parent:ParserRuleContext=None, invokingState:int=-1):
            super().__init__(parent, invokingState)
            self.parser = parser

        def STRING(self):
            return self.getToken(AgtypeParser.STRING, 0)

        def agValue(self):
            return self.getTypedRuleContext(AgtypeParser.AgValueContext,0)


        def getRuleIndex(self):
            return AgtypeParser.RULE_pair

        def enterRule(self, listener:ParseTreeListener):
            if hasattr( listener, "enterPair" ):
                listener.enterPair(self)

        def exitRule(self, listener:ParseTreeListener):
            if hasattr( listener, "exitPair" ):
                listener.exitPair(self)

        def accept(self, visitor:ParseTreeVisitor):
            if hasattr( visitor, "visitPair" ):
                return visitor.visitPair(self)
            else:
                return visitor.visitChildren(self)




    def pair(self):

        localctx = AgtypeParser.PairContext(self, self._ctx, self.state)
        self.enterRule(localctx, 8, self.RULE_pair)
        try:
            self.enterOuterAlt(localctx, 1)
            self.state = 48
            self.match(AgtypeParser.STRING)
            self.state = 49
            self.match(AgtypeParser.T__6)
            self.state = 50
            self.agValue()
        except RecognitionException as re:
            localctx.exception = re
            self._errHandler.reportError(self, re)
            self._errHandler.recover(self, re)
        finally:
            self.exitRule()
        return localctx


    class ArrayContext(ParserRuleContext):
        __slots__ = 'parser'

        def __init__(self, parser, parent:ParserRuleContext=None, invokingState:int=-1):
            super().__init__(parent, invokingState)
            self.parser = parser

        def agValue(self, i:int=None):
            if i is None:
                return self.getTypedRuleContexts(AgtypeParser.AgValueContext)
            else:
                return self.getTypedRuleContext(AgtypeParser.AgValueContext,i)


        def getRuleIndex(self):
            return AgtypeParser.RULE_array

        def enterRule(self, listener:ParseTreeListener):
            if hasattr( listener, "enterArray" ):
                listener.enterArray(self)

        def exitRule(self, listener:ParseTreeListener):
            if hasattr( listener, "exitArray" ):
                listener.exitArray(self)

        def accept(self, visitor:ParseTreeVisitor):
            if hasattr( visitor, "visitArray" ):
                return visitor.visitArray(self)
            else:
                return visitor.visitChildren(self)




    def array(self):

        localctx = AgtypeParser.ArrayContext(self, self._ctx, self.state)
        self.enterRule(localctx, 10, self.RULE_array)
        self._la = 0 # Token type
        try:
            self.state = 65
            self._errHandler.sync(self)
            la_ = self._interp.adaptivePredict(self._input,5,self._ctx)
            if la_ == 1:
                self.enterOuterAlt(localctx, 1)
                self.state = 52
                self.match(AgtypeParser.T__7)
                self.state = 53
                self.agValue()
                self.state = 58
                self._errHandler.sync(self)
                _la = self._input.LA(1)
                while _la==5:
                    self.state = 54
                    self.match(AgtypeParser.T__4)
                    self.state = 55
                    self.agValue()
                    self.state = 60
                    self._errHandler.sync(self)
                    _la = self._input.LA(1)

                self.state = 61
                self.match(AgtypeParser.T__8)
                pass

            elif la_ == 2:
                self.enterOuterAlt(localctx, 2)
                self.state = 63
                self.match(AgtypeParser.T__7)
                self.state = 64
                self.match(AgtypeParser.T__8)
                pass


        except RecognitionException as re:
            localctx.exception = re
            self._errHandler.reportError(self, re)
            self._errHandler.recover(self, re)
        finally:
            self.exitRule()
        return localctx


    class TypeAnnotationContext(ParserRuleContext):
        __slots__ = 'parser'

        def __init__(self, parser, parent:ParserRuleContext=None, invokingState:int=-1):
            super().__init__(parent, invokingState)
            self.parser = parser

        def IDENT(self):
            return self.getToken(AgtypeParser.IDENT, 0)

        def getRuleIndex(self):
            return AgtypeParser.RULE_typeAnnotation

        def enterRule(self, listener:ParseTreeListener):
            if hasattr( listener, "enterTypeAnnotation" ):
                listener.enterTypeAnnotation(self)

        def exitRule(self, listener:ParseTreeListener):
            if hasattr( listener, "exitTypeAnnotation" ):
                listener.exitTypeAnnotation(self)

        def accept(self, visitor:ParseTreeVisitor):
            if hasattr( visitor, "visitTypeAnnotation" ):
                return visitor.visitTypeAnnotation(self)
            else:
                return visitor.visitChildren(self)




    def typeAnnotation(self):

        localctx = AgtypeParser.TypeAnnotationContext(self, self._ctx, self.state)
        self.enterRule(localctx, 12, self.RULE_typeAnnotation)
        try:
            self.enterOuterAlt(localctx, 1)
            self.state = 67
            self.match(AgtypeParser.T__9)
            self.state = 68
            self.match(AgtypeParser.IDENT)
        except RecognitionException as re:
            localctx.exception = re
            self._errHandler.reportError(self, re)
            self._errHandler.recover(self, re)
        finally:
            self.exitRule()
        return localctx


    class FloatLiteralContext(ParserRuleContext):
        __slots__ = 'parser'

        def __init__(self, parser, parent:ParserRuleContext=None, invokingState:int=-1):
            super().__init__(parent, invokingState)
            self.parser = parser

        def RegularFloat(self):
            return self.getToken(AgtypeParser.RegularFloat, 0)

        def ExponentFloat(self):
            return self.getToken(AgtypeParser.ExponentFloat, 0)

        def getRuleIndex(self):
            return AgtypeParser.RULE_floatLiteral

        def enterRule(self, listener:ParseTreeListener):
            if hasattr( listener, "enterFloatLiteral" ):
                listener.enterFloatLiteral(self)

        def exitRule(self, listener:ParseTreeListener):
            if hasattr( listener, "exitFloatLiteral" ):
                listener.exitFloatLiteral(self)

        def accept(self, visitor:ParseTreeVisitor):
            if hasattr( visitor, "visitFloatLiteral" ):
                return visitor.visitFloatLiteral(self)
            else:
                return visitor.visitChildren(self)




    def floatLiteral(self):

        localctx = AgtypeParser.FloatLiteralContext(self, self._ctx, self.state)
        self.enterRule(localctx, 14, self.RULE_floatLiteral)
        self._la = 0 # Token type
        try:
            self.state = 77
            self._errHandler.sync(self)
            token = self._input.LA(1)
            if token in [17]:
                self.enterOuterAlt(localctx, 1)
                self.state = 70
                self.match(AgtypeParser.RegularFloat)
                pass
            elif token in [18]:
                self.enterOuterAlt(localctx, 2)
                self.state = 71
                self.match(AgtypeParser.ExponentFloat)
                pass
            elif token in [11, 12]:
                self.enterOuterAlt(localctx, 3)
                self.state = 73
                self._errHandler.sync(self)
                _la = self._input.LA(1)
                if _la==11:
                    self.state = 72
                    self.match(AgtypeParser.T__10)


                self.state = 75
                self.match(AgtypeParser.T__11)
                pass
            elif token in [13]:
                self.enterOuterAlt(localctx, 4)
                self.state = 76
                self.match(AgtypeParser.T__12)
                pass
            else:
                raise NoViableAltException(self)

        except RecognitionException as re:
            localctx.exception = re
            self._errHandler.reportError(self, re)
            self._errHandler.recover(self, re)
        finally:
            self.exitRule()
        return localctx


