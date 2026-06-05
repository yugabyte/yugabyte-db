/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */
/* Apache AGE output data grammar */
grammar Age;

ageout
   : value
   | vertex
   | edge
   | path
   ;

vertex
   : properties KW_VERTEX
   ;

edge
   : properties KW_EDGE
   ;

path
   : '[' vertex (',' edge ',' vertex)* ']' KW_PATH
   ;

//Keywords
KW_VERTEX : '::vertex';
KW_EDGE : '::edge';
KW_PATH : '::path';
KW_NUMERIC : '::numeric';

// Common Values Rule
value
   : STRING
   | NUMBER
   | NUMERIC
   | FLOAT_EXPR
   | BOOL
   | NULL
   | properties
   | arr
   ;

properties
   : '{' pair (',' pair)* '}'
   | '{' '}'
   ;

pair
   : STRING ':' value
   ;

arr
   : '[' value (',' value)* ']'
   | '[' ']'
   ;

STRING
   : '"' (ESC | SAFECODEPOINT)* '"'
   ;

BOOL
   : 'true'|'false'
   ;

NULL
   : 'null'
   ;


fragment ESC
   : '\\' (["\\/bfnrt] | UNICODE)
   ;
fragment UNICODE
   : 'u' HEX HEX HEX HEX
   ;
fragment HEX
   : [0-9a-fA-F]
   ;
fragment SAFECODEPOINT
   : ~ ["\\\u0000-\u001F]
   ;


NUMBER
   : '-'? INT ('.' [0-9] +)? EXP?
   ;

FLOAT_EXPR
   : 'NaN' | '-Infinity' | 'Infinity'
   ;

NUMERIC
   : '-'? INT ('.' [0-9] +)? EXP? KW_NUMERIC
   ;


fragment INT
   : '0' | [1-9] [0-9]*
   ;

// no leading zeros

fragment EXP
   : [Ee] [+\-]? INT
   ;

// \- since - means "range" inside [...]

WS
   : [ \t\n\r] + -> skip
   ;
