from . import gen
from .gen.ageLexer import ageLexer
from .gen.ageParser import ageParser
from .gen.ageVisitor import ageVisitor
from .models import *
from .exceptions import *
from antlr4 import *
from antlr4.tree.Tree import *
from decimal import Decimal

class ResultHandler:
    def parse(ageData):
        pass

def newResultHandler(query=""):
    resultHandler = Antlr4ResultHandler(None, query)
    return resultHandler

# def buildGraph(cursor, resultHandler:ResultHandler=None):
#     graph = Graph(cursor.query)

#     if resultHandler == None:
#         resultHandler = Antlr4ResultHandler(graph.getVertices(), cursor.query)
   
#     for record in cursor:
#         parsed = resultHandler.parse(record[0])
#         graph.append(parsed)

#     return graph

# def getRows(cursor):
#     vertexCache = dict()
#     resultHandler = Antlr4ResultHandler(vertexCache, cursor.query)
    
#     for record in cursor:
#         yield resultHandler.parse(record[0])
    
#     vertexCache.clear()


# def getSingle(cursor):
#     resultHandler = Antlr4ResultHandler(None, cursor.query)
#     return resultHandler.parse(cursor.fetchone()[0])

def parseAgeValue(value, cursor=None):
    if value is None:
        return None

    resultHandler = Antlr4ResultHandler(None)
    try:
        return resultHandler.parse(value)
    except Exception as ex:
        raise AGTypeError(value)

class Antlr4ResultHandler(ResultHandler):
    def __init__(self, vertexCache, query=None):
        self.lexer = ageLexer()
        self.parser = ageParser(None)
        self.visitor = ResultVisitor(vertexCache)

    def parse(self, ageData):
        self.lexer.inputStream = InputStream(ageData)
        self.parser.setTokenStream(CommonTokenStream(self.lexer))
        self.parser.reset()
        tree = self.parser.ageout()
        parsed = tree.accept(self.visitor)
        return parsed


# print raw result String
class DummyResultHandler(ResultHandler):
    def parse(self, ageData):
        print(ageData)

# default ageout visitor
class ResultVisitor(ageVisitor):
    vertexCache = None

    def __init__(self, cache) -> None:
        super().__init__()
        self.vertexCache = cache

    
    def visitAgeout(self, ctx:ageParser.AgeoutContext):
        return self.visitChildren(ctx)


    # Visit a parse tree produced by ageParser#vertex.
    def visitVertex(self, ctx:ageParser.VertexContext):
        proCtx = ctx.getTypedRuleContext(ageParser.PropertiesContext,0)
        dict = proCtx.accept(self)
        
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


    # Visit a parse tree produced by ageParser#edge.
    def visitEdge(self, ctx:ageParser.EdgeContext):
        edge = Edge()
        proCtx = ctx.getTypedRuleContext(ageParser.PropertiesContext,0)

        dict = proCtx.accept(self)
        edge.id = dict["id"]
        edge.label = dict["label"]
        edge.end_id = dict["end_id"]
        edge.start_id = dict["start_id"]
        edge.properties = dict["properties"]
        
        return edge


    # Visit a parse tree produced by ageParser#path.
    def visitPath(self, ctx:ageParser.PathContext):

        children = []
        
        for child in ctx.children :
            if isinstance(child, ageParser.VertexContext):
                children.append(child.accept(self))
            if isinstance(child, ageParser.EdgeContext):
                children.append(child.accept(self))

        path = Path(children)
        
        return path

    # Visit a parse tree produced by ageParser#value.
    def visitValue(self, ctx:ageParser.ValueContext):
        c = ctx.getChild(0)
        if isinstance(c, ageParser.PropertiesContext) or isinstance(c,ageParser.ArrContext):
            val = c.accept(self)
            return val
        elif isinstance(c, TerminalNodeImpl):
            return getScalar(c.symbol.type, c.getText())
        else:
            return None

    # Visit a parse tree produced by ageParser#properties.
    def visitProperties(self, ctx:ageParser.PropertiesContext):
        props = dict()
        for c in ctx.getChildren():
            if isinstance(c, ageParser.PairContext):
                namVal = self.visitPair(c)
                name = namVal[0]
                valCtx = namVal[1]
                val = valCtx.accept(self) 
                props[name] = val
        return props

    # Visit a parse tree produced by ageParser#pair.
    def visitPair(self, ctx:ageParser.PairContext):
        self.visitChildren(ctx)
        return (ctx.STRING().getText().strip('"') , ctx.value())


    # Visit a parse tree produced by ageParser#arr.
    def visitArr(self, ctx:ageParser.ArrContext):
        li = list()
        for c in ctx.getChildren():
            if not isinstance(c, TerminalNode):
                val = c.accept(self)
                li.append(val)
        return li


def getScalar(agType, text):
    if agType == ageParser.STRING:
        return text.strip('"')
    elif agType == ageParser.INTEGER:
        return int(text)
    elif agType == ageParser.FLOAT:
        return float(text)
    elif agType == ageParser.FLOAT_EXPR:
        if text == 'NaN':
            return float('nan')
        elif text == '-Infinity':
            return float('-inf')
        elif text == 'Infinity':
            return float('inf')
        else:
            return Exception("Unknown float expression:"+text)
    elif agType == ageParser.BOOL:
        return text == "true" or text=="True"
    elif agType == ageParser.NUMERIC:
        return Decimal(text[:len(text)-9])
    elif agType == ageParser.NULL:
        return None
    else :
        raise Exception("Unknown type:"+str(agType))