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

# This class defines a complete listener for a parse tree produced by AgtypeParser.
class AgtypeListener(ParseTreeListener):

    # Enter a parse tree produced by AgtypeParser#agType.
    def enterAgType(self, ctx:AgtypeParser.AgTypeContext):
        pass

    # Exit a parse tree produced by AgtypeParser#agType.
    def exitAgType(self, ctx:AgtypeParser.AgTypeContext):
        pass


    # Enter a parse tree produced by AgtypeParser#agValue.
    def enterAgValue(self, ctx:AgtypeParser.AgValueContext):
        pass

    # Exit a parse tree produced by AgtypeParser#agValue.
    def exitAgValue(self, ctx:AgtypeParser.AgValueContext):
        pass


    # Enter a parse tree produced by AgtypeParser#StringValue.
    def enterStringValue(self, ctx:AgtypeParser.StringValueContext):
        pass

    # Exit a parse tree produced by AgtypeParser#StringValue.
    def exitStringValue(self, ctx:AgtypeParser.StringValueContext):
        pass


    # Enter a parse tree produced by AgtypeParser#IntegerValue.
    def enterIntegerValue(self, ctx:AgtypeParser.IntegerValueContext):
        pass

    # Exit a parse tree produced by AgtypeParser#IntegerValue.
    def exitIntegerValue(self, ctx:AgtypeParser.IntegerValueContext):
        pass


    # Enter a parse tree produced by AgtypeParser#FloatValue.
    def enterFloatValue(self, ctx:AgtypeParser.FloatValueContext):
        pass

    # Exit a parse tree produced by AgtypeParser#FloatValue.
    def exitFloatValue(self, ctx:AgtypeParser.FloatValueContext):
        pass


    # Enter a parse tree produced by AgtypeParser#TrueBoolean.
    def enterTrueBoolean(self, ctx:AgtypeParser.TrueBooleanContext):
        pass

    # Exit a parse tree produced by AgtypeParser#TrueBoolean.
    def exitTrueBoolean(self, ctx:AgtypeParser.TrueBooleanContext):
        pass


    # Enter a parse tree produced by AgtypeParser#FalseBoolean.
    def enterFalseBoolean(self, ctx:AgtypeParser.FalseBooleanContext):
        pass

    # Exit a parse tree produced by AgtypeParser#FalseBoolean.
    def exitFalseBoolean(self, ctx:AgtypeParser.FalseBooleanContext):
        pass


    # Enter a parse tree produced by AgtypeParser#NullValue.
    def enterNullValue(self, ctx:AgtypeParser.NullValueContext):
        pass

    # Exit a parse tree produced by AgtypeParser#NullValue.
    def exitNullValue(self, ctx:AgtypeParser.NullValueContext):
        pass


    # Enter a parse tree produced by AgtypeParser#ObjectValue.
    def enterObjectValue(self, ctx:AgtypeParser.ObjectValueContext):
        pass

    # Exit a parse tree produced by AgtypeParser#ObjectValue.
    def exitObjectValue(self, ctx:AgtypeParser.ObjectValueContext):
        pass


    # Enter a parse tree produced by AgtypeParser#ArrayValue.
    def enterArrayValue(self, ctx:AgtypeParser.ArrayValueContext):
        pass

    # Exit a parse tree produced by AgtypeParser#ArrayValue.
    def exitArrayValue(self, ctx:AgtypeParser.ArrayValueContext):
        pass


    # Enter a parse tree produced by AgtypeParser#obj.
    def enterObj(self, ctx:AgtypeParser.ObjContext):
        pass

    # Exit a parse tree produced by AgtypeParser#obj.
    def exitObj(self, ctx:AgtypeParser.ObjContext):
        pass


    # Enter a parse tree produced by AgtypeParser#pair.
    def enterPair(self, ctx:AgtypeParser.PairContext):
        pass

    # Exit a parse tree produced by AgtypeParser#pair.
    def exitPair(self, ctx:AgtypeParser.PairContext):
        pass


    # Enter a parse tree produced by AgtypeParser#array.
    def enterArray(self, ctx:AgtypeParser.ArrayContext):
        pass

    # Exit a parse tree produced by AgtypeParser#array.
    def exitArray(self, ctx:AgtypeParser.ArrayContext):
        pass


    # Enter a parse tree produced by AgtypeParser#typeAnnotation.
    def enterTypeAnnotation(self, ctx:AgtypeParser.TypeAnnotationContext):
        pass

    # Exit a parse tree produced by AgtypeParser#typeAnnotation.
    def exitTypeAnnotation(self, ctx:AgtypeParser.TypeAnnotationContext):
        pass


    # Enter a parse tree produced by AgtypeParser#floatLiteral.
    def enterFloatLiteral(self, ctx:AgtypeParser.FloatLiteralContext):
        pass

    # Exit a parse tree produced by AgtypeParser#floatLiteral.
    def exitFloatLiteral(self, ctx:AgtypeParser.FloatLiteralContext):
        pass



del AgtypeParser