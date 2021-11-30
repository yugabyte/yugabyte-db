/* Apache AGE output data grammer */
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
   