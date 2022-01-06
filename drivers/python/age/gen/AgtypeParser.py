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
# Generated from Agtype.g4 by ANTLR 4.9.2
# encoding: utf-8
from antlr4 import *
from io import StringIO
import sys
if sys.version_info[1] > 5:
	from typing import TextIO
else:
	from typing.io import TextIO


def serializedATN():
    with StringIO() as buf:
        buf.write("\3\u608b\ua72a\u8133\ub9ed\u417c\u3be7\u7786\u5964\3\25")
        buf.write("R\4\2\t\2\4\3\t\3\4\4\t\4\4\5\t\5\4\6\t\6\4\7\t\7\4\b")
        buf.write("\t\b\4\t\t\t\3\2\3\2\3\2\3\3\3\3\5\3\30\n\3\3\4\3\4\3")
        buf.write("\4\3\4\3\4\3\4\3\4\3\4\5\4\"\n\4\3\5\3\5\3\5\3\5\7\5(")
        buf.write("\n\5\f\5\16\5+\13\5\3\5\3\5\3\5\3\5\5\5\61\n\5\3\6\3\6")
        buf.write("\3\6\3\6\3\7\3\7\3\7\3\7\7\7;\n\7\f\7\16\7>\13\7\3\7\3")
        buf.write("\7\3\7\3\7\5\7D\n\7\3\b\3\b\3\b\3\t\3\t\3\t\5\tL\n\t\3")
        buf.write("\t\3\t\5\tP\n\t\3\t\2\2\n\2\4\6\b\n\f\16\20\2\2\2Y\2\22")
        buf.write("\3\2\2\2\4\25\3\2\2\2\6!\3\2\2\2\b\60\3\2\2\2\n\62\3\2")
        buf.write("\2\2\fC\3\2\2\2\16E\3\2\2\2\20O\3\2\2\2\22\23\5\4\3\2")
        buf.write("\23\24\7\2\2\3\24\3\3\2\2\2\25\27\5\6\4\2\26\30\5\16\b")
        buf.write("\2\27\26\3\2\2\2\27\30\3\2\2\2\30\5\3\2\2\2\31\"\7\21")
        buf.write("\2\2\32\"\7\22\2\2\33\"\5\20\t\2\34\"\7\3\2\2\35\"\7\4")
        buf.write("\2\2\36\"\7\5\2\2\37\"\5\b\5\2 \"\5\f\7\2!\31\3\2\2\2")
        buf.write("!\32\3\2\2\2!\33\3\2\2\2!\34\3\2\2\2!\35\3\2\2\2!\36\3")
        buf.write("\2\2\2!\37\3\2\2\2! \3\2\2\2\"\7\3\2\2\2#$\7\6\2\2$)\5")
        buf.write("\n\6\2%&\7\7\2\2&(\5\n\6\2\'%\3\2\2\2(+\3\2\2\2)\'\3\2")
        buf.write("\2\2)*\3\2\2\2*,\3\2\2\2+)\3\2\2\2,-\7\b\2\2-\61\3\2\2")
        buf.write("\2./\7\6\2\2/\61\7\b\2\2\60#\3\2\2\2\60.\3\2\2\2\61\t")
        buf.write("\3\2\2\2\62\63\7\21\2\2\63\64\7\t\2\2\64\65\5\4\3\2\65")
        buf.write("\13\3\2\2\2\66\67\7\n\2\2\67<\5\4\3\289\7\7\2\29;\5\4")
        buf.write("\3\2:8\3\2\2\2;>\3\2\2\2<:\3\2\2\2<=\3\2\2\2=?\3\2\2\2")
        buf.write("><\3\2\2\2?@\7\13\2\2@D\3\2\2\2AB\7\n\2\2BD\7\13\2\2C")
        buf.write("\66\3\2\2\2CA\3\2\2\2D\r\3\2\2\2EF\7\f\2\2FG\7\20\2\2")
        buf.write("G\17\3\2\2\2HP\7\23\2\2IP\7\24\2\2JL\7\r\2\2KJ\3\2\2\2")
        buf.write("KL\3\2\2\2LM\3\2\2\2MP\7\16\2\2NP\7\17\2\2OH\3\2\2\2O")
        buf.write("I\3\2\2\2OK\3\2\2\2ON\3\2\2\2P\21\3\2\2\2\n\27!)\60<C")
        buf.write("KO")
        return buf.getvalue()


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
        self.checkVersion("4.9.2")
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
            if _la==AgtypeParser.T__9:
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
            if token in [AgtypeParser.STRING]:
                localctx = AgtypeParser.StringValueContext(self, localctx)
                self.enterOuterAlt(localctx, 1)
                self.state = 23
                self.match(AgtypeParser.STRING)
                pass
            elif token in [AgtypeParser.INTEGER]:
                localctx = AgtypeParser.IntegerValueContext(self, localctx)
                self.enterOuterAlt(localctx, 2)
                self.state = 24
                self.match(AgtypeParser.INTEGER)
                pass
            elif token in [AgtypeParser.T__10, AgtypeParser.T__11, AgtypeParser.T__12, AgtypeParser.RegularFloat, AgtypeParser.ExponentFloat]:
                localctx = AgtypeParser.FloatValueContext(self, localctx)
                self.enterOuterAlt(localctx, 3)
                self.state = 25
                self.floatLiteral()
                pass
            elif token in [AgtypeParser.T__0]:
                localctx = AgtypeParser.TrueBooleanContext(self, localctx)
                self.enterOuterAlt(localctx, 4)
                self.state = 26
                self.match(AgtypeParser.T__0)
                pass
            elif token in [AgtypeParser.T__1]:
                localctx = AgtypeParser.FalseBooleanContext(self, localctx)
                self.enterOuterAlt(localctx, 5)
                self.state = 27
                self.match(AgtypeParser.T__1)
                pass
            elif token in [AgtypeParser.T__2]:
                localctx = AgtypeParser.NullValueContext(self, localctx)
                self.enterOuterAlt(localctx, 6)
                self.state = 28
                self.match(AgtypeParser.T__2)
                pass
            elif token in [AgtypeParser.T__3]:
                localctx = AgtypeParser.ObjectValueContext(self, localctx)
                self.enterOuterAlt(localctx, 7)
                self.state = 29
                self.obj()
                pass
            elif token in [AgtypeParser.T__7]:
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
                while _la==AgtypeParser.T__4:
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
                while _la==AgtypeParser.T__4:
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
            if token in [AgtypeParser.RegularFloat]:
                self.enterOuterAlt(localctx, 1)
                self.state = 70
                self.match(AgtypeParser.RegularFloat)
                pass
            elif token in [AgtypeParser.ExponentFloat]:
                self.enterOuterAlt(localctx, 2)
                self.state = 71
                self.match(AgtypeParser.ExponentFloat)
                pass
            elif token in [AgtypeParser.T__10, AgtypeParser.T__11]:
                self.enterOuterAlt(localctx, 3)
                self.state = 73
                self._errHandler.sync(self)
                _la = self._input.LA(1)
                if _la==AgtypeParser.T__10:
                    self.state = 72
                    self.match(AgtypeParser.T__10)


                self.state = 75
                self.match(AgtypeParser.T__11)
                pass
            elif token in [AgtypeParser.T__12]:
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





