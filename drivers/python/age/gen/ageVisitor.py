# Generated from age.g4 by ANTLR 4.9
from antlr4 import *
if __name__ is not None and "." in __name__:
    from .ageParser import ageParser
else:
    from ageParser import ageParser

# This class defines a complete generic visitor for a parse tree produced by ageParser.

class ageVisitor(ParseTreeVisitor):

    # Visit a parse tree produced by ageParser#ageout.
    def visitAgeout(self, ctx:ageParser.AgeoutContext):
        return self.visitChildren(ctx)


    # Visit a parse tree produced by ageParser#vertex.
    def visitVertex(self, ctx:ageParser.VertexContext):
        return self.visitChildren(ctx)


    # Visit a parse tree produced by ageParser#edge.
    def visitEdge(self, ctx:ageParser.EdgeContext):
        return self.visitChildren(ctx)


    # Visit a parse tree produced by ageParser#path.
    def visitPath(self, ctx:ageParser.PathContext):
        return self.visitChildren(ctx)


    # Visit a parse tree produced by ageParser#value.
    def visitValue(self, ctx:ageParser.ValueContext):
        return self.visitChildren(ctx)


    # Visit a parse tree produced by ageParser#properties.
    def visitProperties(self, ctx:ageParser.PropertiesContext):
        return self.visitChildren(ctx)


    # Visit a parse tree produced by ageParser#pair.
    def visitPair(self, ctx:ageParser.PairContext):
        return self.visitChildren(ctx)


    # Visit a parse tree produced by ageParser#arr.
    def visitArr(self, ctx:ageParser.ArrContext):
        return self.visitChildren(ctx)



del ageParser