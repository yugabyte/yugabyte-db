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
from . import gen
from .gen.AgtypeLexer import AgtypeLexer
from .gen.AgtypeParser import AgtypeParser
from .gen.AgtypeVisitor import AgtypeVisitor
from .models import *
from .exceptions import *
from antlr4 import InputStream, CommonTokenStream, ParserRuleContext
from antlr4.tree.Tree import TerminalNode
from decimal import Decimal

resultHandler = None

class ResultHandler:
    def parse(ageData):
        pass

def newResultHandler(query=""):
    resultHandler = Antlr4ResultHandler(None, query)
    return resultHandler

def parseAgeValue(value, cursor=None):
    if value is None:
        return None

    global resultHandler
    if (resultHandler == None):
        resultHandler = Antlr4ResultHandler(None)
    try:
        return resultHandler.parse(value)
    except Exception as ex:
        raise AGTypeError(value, ex)


class Antlr4ResultHandler(ResultHandler):
    def __init__(self, vertexCache, query=None):
        self.lexer = AgtypeLexer()
        self.parser = AgtypeParser(None)
        self.visitor = ResultVisitor(vertexCache)

    def parse(self, ageData):
        if not ageData:
            return None
        # print("Parse::", ageData)

        self.lexer.inputStream = InputStream(ageData)
        self.parser.setTokenStream(CommonTokenStream(self.lexer))
        self.parser.reset()
        tree = self.parser.agType()
        parsed = tree.accept(self.visitor)
        return parsed


# print raw result String
class DummyResultHandler(ResultHandler):
    def parse(self, ageData):
        print(ageData)

# default agType visitor
class ResultVisitor(AgtypeVisitor):
    vertexCache = None

    def __init__(self, cache) -> None:
        super().__init__()
        self.vertexCache = cache

    
    def visitAgType(self, ctx:AgtypeParser.AgTypeContext):
        agVal = ctx.agValue()
        if agVal != None:
            obj = ctx.agValue().accept(self)
            return obj

        return None

    def visitAgValue(self, ctx:AgtypeParser.AgValueContext):
        annoCtx = ctx.typeAnnotation()
        valueCtx = ctx.value()

        if annoCtx is not None:
            annoCtx.accept(self)
            anno = annoCtx.IDENT().getText()
            return self.handleAnnotatedValue(anno, valueCtx)
        else:
            return valueCtx.accept(self)


    # Visit a parse tree produced by AgtypeParser#StringValue.
    def visitStringValue(self, ctx:AgtypeParser.StringValueContext):
        return ctx.STRING().getText().strip('"')


    # Visit a parse tree produced by AgtypeParser#IntegerValue.
    def visitIntegerValue(self, ctx:AgtypeParser.IntegerValueContext):
        return int(ctx.INTEGER().getText())

    # Visit a parse tree produced by AgtypeParser#floatLiteral.
    def visitFloatLiteral(self, ctx:AgtypeParser.FloatLiteralContext):
        c = ctx.getChild(0)
        tp = c.symbol.type
        text = ctx.getText()
        if tp == AgtypeParser.RegularFloat:
            return float(text)
        elif tp == AgtypeParser.ExponentFloat:
            return float(text)
        else:
            if text == 'NaN':
                return float('nan')
            elif text == '-Infinity':
                return float('-inf')
            elif text == 'Infinity':
                return float('inf')
            else:
                return Exception("Unknown float expression:"+text)
        

    # Visit a parse tree produced by AgtypeParser#TrueBoolean.
    def visitTrueBoolean(self, ctx:AgtypeParser.TrueBooleanContext):
        return True


    # Visit a parse tree produced by AgtypeParser#FalseBoolean.
    def visitFalseBoolean(self, ctx:AgtypeParser.FalseBooleanContext):
        return False


    # Visit a parse tree produced by AgtypeParser#NullValue.
    def visitNullValue(self, ctx:AgtypeParser.NullValueContext):
        return None


    # Visit a parse tree produced by AgtypeParser#obj.
    def visitObj(self, ctx:AgtypeParser.ObjContext):
        obj = dict()
        for c in ctx.getChildren():
            if isinstance(c, AgtypeParser.PairContext):
                namVal = self.visitPair(c)
                name = namVal[0]
                valCtx = namVal[1]
                val = valCtx.accept(self) 
                obj[name] = val
        return obj


    # Visit a parse tree produced by AgtypeParser#pair.
    def visitPair(self, ctx:AgtypeParser.PairContext):
        self.visitChildren(ctx)
        return (ctx.STRING().getText().strip('"') , ctx.agValue())


    # Visit a parse tree produced by AgtypeParser#array.
    def visitArray(self, ctx:AgtypeParser.ArrayContext):
        li = list()
        for c in ctx.getChildren():
            if not isinstance(c, TerminalNode):
                val = c.accept(self)
                li.append(val)
        return li

    def handleAnnotatedValue(self, anno:str, ctx:ParserRuleContext):
        if anno == "numeric":
            return Decimal(ctx.getText())
        elif anno == "vertex":
            dict = ctx.accept(self)
            vid = dict["id"]
            vertex = None
            if self.vertexCache != None and vid in self.vertexCache :
                vertex = self.vertexCache[vid]
            else:
                vertex = Vertex()
                vertex.id = dict["id"]
                vertex.label = dict["label"]
                vertex.properties = dict["properties"]
            
            if self.vertexCache != None:
                self.vertexCache[vid] = vertex

            return vertex
        
        elif anno == "edge":
            edge = Edge()
            dict = ctx.accept(self)
            edge.id = dict["id"]
            edge.label = dict["label"]
            edge.end_id = dict["end_id"]
            edge.start_id = dict["start_id"]
            edge.properties = dict["properties"]
            
            return edge

        elif anno == "path":
            arr = ctx.accept(self)
            path = Path(arr)
            
            return path

        return ctx.accept(self)
