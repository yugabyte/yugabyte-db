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

from antlr4 import *
if __name__ is not None and "." in __name__:
    from .AgtypeParser import AgtypeParser
else:
    from AgtypeParser import AgtypeParser

# This class defines a complete generic visitor for a parse tree produced by AgtypeParser.

class AgtypeVisitor(ParseTreeVisitor):

    # Visit a parse tree produced by AgtypeParser#agType.
    def visitAgType(self, ctx:AgtypeParser.AgTypeContext):
        return self.visitChildren(ctx)


    # Visit a parse tree produced by AgtypeParser#agValue.
    def visitAgValue(self, ctx:AgtypeParser.AgValueContext):
        return self.visitChildren(ctx)


    # Visit a parse tree produced by AgtypeParser#StringValue.
    def visitStringValue(self, ctx:AgtypeParser.StringValueContext):
        return self.visitChildren(ctx)


    # Visit a parse tree produced by AgtypeParser#IntegerValue.
    def visitIntegerValue(self, ctx:AgtypeParser.IntegerValueContext):
        return self.visitChildren(ctx)


    # Visit a parse tree produced by AgtypeParser#FloatValue.
    def visitFloatValue(self, ctx:AgtypeParser.FloatValueContext):
        return self.visitChildren(ctx)


    # Visit a parse tree produced by AgtypeParser#TrueBoolean.
    def visitTrueBoolean(self, ctx:AgtypeParser.TrueBooleanContext):
        return self.visitChildren(ctx)


    # Visit a parse tree produced by AgtypeParser#FalseBoolean.
    def visitFalseBoolean(self, ctx:AgtypeParser.FalseBooleanContext):
        return self.visitChildren(ctx)


    # Visit a parse tree produced by AgtypeParser#NullValue.
    def visitNullValue(self, ctx:AgtypeParser.NullValueContext):
        return self.visitChildren(ctx)


    # Visit a parse tree produced by AgtypeParser#ObjectValue.
    def visitObjectValue(self, ctx:AgtypeParser.ObjectValueContext):
        return self.visitChildren(ctx)


    # Visit a parse tree produced by AgtypeParser#ArrayValue.
    def visitArrayValue(self, ctx:AgtypeParser.ArrayValueContext):
        return self.visitChildren(ctx)


    # Visit a parse tree produced by AgtypeParser#obj.
    def visitObj(self, ctx:AgtypeParser.ObjContext):
        return self.visitChildren(ctx)


    # Visit a parse tree produced by AgtypeParser#pair.
    def visitPair(self, ctx:AgtypeParser.PairContext):
        return self.visitChildren(ctx)


    # Visit a parse tree produced by AgtypeParser#array.
    def visitArray(self, ctx:AgtypeParser.ArrayContext):
        return self.visitChildren(ctx)


    # Visit a parse tree produced by AgtypeParser#typeAnnotation.
    def visitTypeAnnotation(self, ctx:AgtypeParser.TypeAnnotationContext):
        return self.visitChildren(ctx)


    # Visit a parse tree produced by AgtypeParser#floatLiteral.
    def visitFloatLiteral(self, ctx:AgtypeParser.FloatLiteralContext):
        return self.visitChildren(ctx)



del AgtypeParser