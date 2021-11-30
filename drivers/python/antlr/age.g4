/* Apache AGE output data grammer */

grammar age;

ageout
   : value
   | vertex
   | edge
   | path
   ;

vertex
   : properties ANNO_VERTEX
   ;

edge
   : properties ANNO_EDGE
   ;

path
   : '[' vertex (',' edge ',' vertex)* ']' ANNO_PATH
   ;

//ANNOTATIONS
ANNO_VERTEX : '::vertex';
ANNO_EDGE : '::edge';
ANNO_PATH : '::path';
ANNO_NUMERIC : '::numeric';

// Common Values Rule
value
   : STRING
   | INTEGER
   | FLOAT
   | FLOAT_EXPR
   | NUMERIC
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

NUMERIC
   : '-'? INT ('.' [0-9] +)? EXP? ANNO_NUMERIC
   ;

INTEGER
  : '-'? INT
  ;

FLOAT
   : '-'? INT '.' DIGITS
   | '-'? INT '.' DIGITS? EXP
   ;

FLOAT_EXPR
   : '-'? 'Infinity'
   | 'NaN'
   ;

fragment INT
   : '0' | [1-9] DIGITS
   ;

fragment DIGITS
   : [0-9]*
   ;
   
fragment EXP
  : [Ee][+-]? [0-9]+
  ;


// \- since - means "range" inside [...]

WS
   : [ \t\n\r] + -> skip
   ;

// -----------------------------------

