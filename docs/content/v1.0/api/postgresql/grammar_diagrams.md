---
title: Grammar Diagrams
summary: Diagrams of the grammar rules.
---

## DDL Statements

### create_database
```
create_database = 'CREATE' 'DATABASE' database_name;
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="296" height="35" viewbox="0 0 296 35"><path class="connector" d="M0 22h5m67 0h10m84 0h10m115 0h5"/><rect class="literal" x="5" y="5" width="67" height="25" rx="7"/><text class="text" x="15" y="22">CREATE</text><rect class="literal" x="82" y="5" width="84" height="25" rx="7"/><text class="text" x="92" y="22">DATABASE</text><a xlink:href="../grammar_diagrams#database-name"><rect class="rule" x="176" y="5" width="115" height="25"/><text class="text" x="186" y="22">database_name</text></a></svg>

### create_table
```
create_table = 'CREATE' 'TABLE' [ 'IF' 'NOT' 'EXISTS' ] table_name '(' table_element { ',' table_element } ')';
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="683" height="80" viewbox="0 0 683 80"><path class="connector" d="M0 52h5m67 0h10m58 0h30m32 0h10m45 0h10m64 0h20m-196 0q5 0 5 5v8q0 5 5 5h171q5 0 5-5v-8q0-5 5-5m5 0h10m91 0h10m25 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h46m24 0h46q5 0 5 5v20q0 5-5 5m-5 0h30m25 0h5"/><rect class="literal" x="5" y="35" width="67" height="25" rx="7"/><text class="text" x="15" y="52">CREATE</text><rect class="literal" x="82" y="35" width="58" height="25" rx="7"/><text class="text" x="92" y="52">TABLE</text><rect class="literal" x="170" y="35" width="32" height="25" rx="7"/><text class="text" x="180" y="52">IF</text><rect class="literal" x="212" y="35" width="45" height="25" rx="7"/><text class="text" x="222" y="52">NOT</text><rect class="literal" x="267" y="35" width="64" height="25" rx="7"/><text class="text" x="277" y="52">EXISTS</text><a xlink:href="../grammar_diagrams#table-name"><rect class="rule" x="361" y="35" width="91" height="25"/><text class="text" x="371" y="52">table_name</text></a><rect class="literal" x="462" y="35" width="25" height="25" rx="7"/><text class="text" x="472" y="52">(</text><rect class="literal" x="558" y="5" width="24" height="25" rx="7"/><text class="text" x="568" y="22">,</text><a xlink:href="../grammar_diagrams#table-element"><rect class="rule" x="517" y="35" width="106" height="25"/><text class="text" x="527" y="52">table_element</text></a><rect class="literal" x="653" y="35" width="25" height="25" rx="7"/><text class="text" x="663" y="52">)</text></svg>

### table_element
```
table_element = table_column | table_constraints;
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="173" height="65" viewbox="0 0 173 65"><path class="connector" d="M0 22h25m101 0h42m-158 0q5 0 5 5v20q0 5 5 5h5m123 0h5q5 0 5-5v-20q0-5 5-5m5 0h5"/><a xlink:href="../grammar_diagrams#table-column"><rect class="rule" x="25" y="5" width="101" height="25"/><text class="text" x="35" y="22">table_column</text></a><a xlink:href="../grammar_diagrams#table-constraints"><rect class="rule" x="25" y="35" width="123" height="25"/><text class="text" x="35" y="52">table_constraints</text></a></svg>

### table_column
```
table_column = column_name column_type { column_constraint };
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="446" height="65" viewbox="0 0 446 65"><path class="connector" d="M0 37h5m106 0h10m98 0h50m-5 0q-5 0-5-5v-17q0-5 5-5h142q5 0 5 5v17q0 5-5 5m-5 0h40m-207 0q5 0 5 5v8q0 5 5 5h182q5 0 5-5v-8q0-5 5-5m5 0h5"/><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="5" y="20" width="106" height="25"/><text class="text" x="15" y="37">column_name</text></a><a xlink:href="../grammar_diagrams#column-type"><rect class="rule" x="121" y="20" width="98" height="25"/><text class="text" x="131" y="37">column_type</text></a><a xlink:href="../grammar_diagrams#column-constraint"><rect class="rule" x="269" y="20" width="132" height="25"/><text class="text" x="279" y="37">column_constraint</text></a></svg>

### column_constraint
```
column_constraint = 'PRIMARY' 'KEY';
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="136" height="35" viewbox="0 0 136 35"><path class="connector" d="M0 22h5m73 0h10m43 0h5"/><rect class="literal" x="5" y="5" width="73" height="25" rx="7"/><text class="text" x="15" y="22">PRIMARY</text><rect class="literal" x="88" y="5" width="43" height="25" rx="7"/><text class="text" x="98" y="22">KEY</text></svg>

### table_constraints
```
table_constraints = 'PRIMARY' 'KEY' '(' column_list ')';
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="305" height="35" viewbox="0 0 305 35"><path class="connector" d="M0 22h5m73 0h10m43 0h10m25 0h10m89 0h10m25 0h5"/><rect class="literal" x="5" y="5" width="73" height="25" rx="7"/><text class="text" x="15" y="22">PRIMARY</text><rect class="literal" x="88" y="5" width="43" height="25" rx="7"/><text class="text" x="98" y="22">KEY</text><rect class="literal" x="141" y="5" width="25" height="25" rx="7"/><text class="text" x="151" y="22">(</text><a xlink:href="../grammar_diagrams#column-list"><rect class="rule" x="176" y="5" width="89" height="25"/><text class="text" x="186" y="22">column_list</text></a><rect class="literal" x="275" y="5" width="25" height="25" rx="7"/><text class="text" x="285" y="22">)</text></svg>

### column_list
```
column_list = column_name { ',' column_name };
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="156" height="65" viewbox="0 0 156 65"><path class="connector" d="M0 52h25m-5 0q-5 0-5-5v-20q0-5 5-5h46m24 0h46q5 0 5 5v20q0 5-5 5m-5 0h25"/><rect class="literal" x="66" y="5" width="24" height="25" rx="7"/><text class="text" x="76" y="22">,</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="25" y="35" width="106" height="25"/><text class="text" x="35" y="52">column_name</text></a></svg>

### drop_database
```
drop_database = 'DROP' 'DATABASE' [ 'IF' 'EXISTS' ] database_name;
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="438" height="50" viewbox="0 0 438 50"><path class="connector" d="M0 22h5m53 0h10m84 0h30m32 0h10m64 0h20m-141 0q5 0 5 5v8q0 5 5 5h116q5 0 5-5v-8q0-5 5-5m5 0h10m115 0h5"/><rect class="literal" x="5" y="5" width="53" height="25" rx="7"/><text class="text" x="15" y="22">DROP</text><rect class="literal" x="68" y="5" width="84" height="25" rx="7"/><text class="text" x="78" y="22">DATABASE</text><rect class="literal" x="182" y="5" width="32" height="25" rx="7"/><text class="text" x="192" y="22">IF</text><rect class="literal" x="224" y="5" width="64" height="25" rx="7"/><text class="text" x="234" y="22">EXISTS</text><a xlink:href="../grammar_diagrams#database-name"><rect class="rule" x="318" y="5" width="115" height="25"/><text class="text" x="328" y="22">database_name</text></a></svg>

### drop_table
```
drop_table = 'DROP' 'TABLE' [ 'IF' 'EXISTS' ] table_name;
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="388" height="50" viewbox="0 0 388 50"><path class="connector" d="M0 22h5m53 0h10m58 0h30m32 0h10m64 0h20m-141 0q5 0 5 5v8q0 5 5 5h116q5 0 5-5v-8q0-5 5-5m5 0h10m91 0h5"/><rect class="literal" x="5" y="5" width="53" height="25" rx="7"/><text class="text" x="15" y="22">DROP</text><rect class="literal" x="68" y="5" width="58" height="25" rx="7"/><text class="text" x="78" y="22">TABLE</text><rect class="literal" x="156" y="5" width="32" height="25" rx="7"/><text class="text" x="166" y="22">IF</text><rect class="literal" x="198" y="5" width="64" height="25" rx="7"/><text class="text" x="208" y="22">EXISTS</text><a xlink:href="../grammar_diagrams#table-name"><rect class="rule" x="292" y="5" width="91" height="25"/><text class="text" x="302" y="22">table_name</text></a></svg>

## DML Statements

### delete
```
delete = 'DELETE' 'FROM' table_name 'WHERE' where_expression;
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="455" height="35" viewbox="0 0 455 35"><path class="connector" d="M0 22h5m67 0h10m54 0h10m91 0h10m65 0h10m128 0h5"/><rect class="literal" x="5" y="5" width="67" height="25" rx="7"/><text class="text" x="15" y="22">DELETE</text><rect class="literal" x="82" y="5" width="54" height="25" rx="7"/><text class="text" x="92" y="22">FROM</text><a xlink:href="../grammar_diagrams#table-name"><rect class="rule" x="146" y="5" width="91" height="25"/><text class="text" x="156" y="22">table_name</text></a><rect class="literal" x="247" y="5" width="65" height="25" rx="7"/><text class="text" x="257" y="22">WHERE</text><a xlink:href="../grammar_diagrams#where-expression"><rect class="rule" x="322" y="5" width="128" height="25"/><text class="text" x="332" y="22">where_expression</text></a></svg>

### where_expression
```
where_expression = ( column_name ( '<' | '<=' | '=' | '>=' | '>' | 'IN' | 'NOT IN' ) expression ) { 'AND' ( column_name ( '<' | '<=' | '=' | '>=' | '>' | 'IN' | 'NOT IN' ) expression ) };
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="362" height="245" viewbox="0 0 362 245"><path class="connector" d="M0 52h25m-5 0q-5 0-5-5v-20q0-5 5-5h138m46 0h138q5 0 5 5v20q0 5-5 5m-211 0h30m30 0h53m-93 25q0 5 5 5h5m40 0h28q5 0 5-5m-83 30q0 5 5 5h5m30 0h38q5 0 5-5m-83 30q0 5 5 5h5m40 0h28q5 0 5-5m-83 30q0 5 5 5h5m30 0h38q5 0 5-5m-83 30q0 5 5 5h5m34 0h34q5 0 5-5m-88-145q5 0 5 5v170q0 5 5 5h5m63 0h5q5 0 5-5v-170q0-5 5-5m5 0h10m83 0h25"/><rect class="literal" x="158" y="5" width="46" height="25" rx="7"/><text class="text" x="168" y="22">AND</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="25" y="35" width="106" height="25"/><text class="text" x="35" y="52">column_name</text></a><rect class="literal" x="161" y="35" width="30" height="25" rx="7"/><text class="text" x="171" y="52">&lt;</text><rect class="literal" x="161" y="65" width="40" height="25" rx="7"/><text class="text" x="171" y="82">&lt;=</text><rect class="literal" x="161" y="95" width="30" height="25" rx="7"/><text class="text" x="171" y="112">=</text><rect class="literal" x="161" y="125" width="40" height="25" rx="7"/><text class="text" x="171" y="142">&gt;=</text><rect class="literal" x="161" y="155" width="30" height="25" rx="7"/><text class="text" x="171" y="172">&gt;</text><rect class="literal" x="161" y="185" width="34" height="25" rx="7"/><text class="text" x="171" y="202">IN</text><rect class="literal" x="161" y="215" width="63" height="25" rx="7"/><text class="text" x="171" y="232">NOT IN</text><a xlink:href="../grammar_diagrams#expression"><rect class="rule" x="254" y="35" width="83" height="25"/><text class="text" x="264" y="52">expression</text></a></svg>

### insert
```
insert = 'INSERT' 'INTO' table_name [ '(' column { ',' column } ')' ] 'VALUES' '(' value { ',' value } ')';
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="710" height="80" viewbox="0 0 710 80"><path class="connector" d="M0 52h5m65 0h10m50 0h10m91 0h30m25 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h25m24 0h25q5 0 5 5v20q0 5-5 5m-5 0h30m25 0h20m-209 0q5 0 5 5v8q0 5 5 5h184q5 0 5-5v-8q0-5 5-5m5 0h10m68 0h10m25 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h19m24 0h19q5 0 5 5v20q0 5-5 5m-5 0h30m25 0h5"/><rect class="literal" x="5" y="35" width="65" height="25" rx="7"/><text class="text" x="15" y="52">INSERT</text><rect class="literal" x="80" y="35" width="50" height="25" rx="7"/><text class="text" x="90" y="52">INTO</text><a xlink:href="../grammar_diagrams#table-name"><rect class="rule" x="140" y="35" width="91" height="25"/><text class="text" x="150" y="52">table_name</text></a><rect class="literal" x="261" y="35" width="25" height="25" rx="7"/><text class="text" x="271" y="52">(</text><rect class="literal" x="336" y="5" width="24" height="25" rx="7"/><text class="text" x="346" y="22">,</text><a xlink:href="../grammar_diagrams#column"><rect class="rule" x="316" y="35" width="64" height="25"/><text class="text" x="326" y="52">column</text></a><rect class="literal" x="410" y="35" width="25" height="25" rx="7"/><text class="text" x="420" y="52">)</text><rect class="literal" x="465" y="35" width="68" height="25" rx="7"/><text class="text" x="475" y="52">VALUES</text><rect class="literal" x="543" y="35" width="25" height="25" rx="7"/><text class="text" x="553" y="52">(</text><rect class="literal" x="612" y="5" width="24" height="25" rx="7"/><text class="text" x="622" y="22">,</text><a xlink:href="../grammar_diagrams#value"><rect class="rule" x="598" y="35" width="52" height="25"/><text class="text" x="608" y="52">value</text></a><rect class="literal" x="680" y="35" width="25" height="25" rx="7"/><text class="text" x="690" y="52">)</text></svg>

### select
```
select = 'SELECT' [ 'DISTINCT' ] ( '*' | column_name { ',' column_name } ) 'FROM' table_name [ 'WHERE' where_expression ];
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="818" height="95" viewbox="0 0 818 95"><path class="connector" d="M0 22h5m66 0h30m78 0h20m-113 0q5 0 5 5v8q0 5 5 5h88q5 0 5-5v-8q0-5 5-5m5 0h30m28 0h138m-181 0q5 0 5 5v50q0 5 5 5h25m-5 0q-5 0-5-5v-20q0-5 5-5h46m24 0h46q5 0 5 5v20q0 5-5 5m-5 0h25q5 0 5-5v-50q0-5 5-5m5 0h10m54 0h10m91 0h30m65 0h10m128 0h20m-238 0q5 0 5 5v8q0 5 5 5h213q5 0 5-5v-8q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="5" width="66" height="25" rx="7"/><text class="text" x="15" y="22">SELECT</text><rect class="literal" x="101" y="5" width="78" height="25" rx="7"/><text class="text" x="111" y="22">DISTINCT</text><rect class="literal" x="229" y="5" width="28" height="25" rx="7"/><text class="text" x="239" y="22">*</text><rect class="literal" x="290" y="35" width="24" height="25" rx="7"/><text class="text" x="300" y="52">,</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="249" y="65" width="106" height="25"/><text class="text" x="259" y="82">column_name</text></a><rect class="literal" x="405" y="5" width="54" height="25" rx="7"/><text class="text" x="415" y="22">FROM</text><a xlink:href="../grammar_diagrams#table-name"><rect class="rule" x="469" y="5" width="91" height="25"/><text class="text" x="479" y="22">table_name</text></a><rect class="literal" x="590" y="5" width="65" height="25" rx="7"/><text class="text" x="600" y="22">WHERE</text><a xlink:href="../grammar_diagrams#where-expression"><rect class="rule" x="665" y="5" width="128" height="25"/><text class="text" x="675" y="22">where_expression</text></a></svg>

### update
```
update = 'UPDATE' table_name 'SET' assignment { ',' assignment } 'WHERE' where_expression;
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="584" height="65" viewbox="0 0 584 65"><path class="connector" d="M0 52h5m68 0h10m91 0h10m43 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h37m24 0h38q5 0 5 5v20q0 5-5 5m-5 0h30m65 0h10m128 0h5"/><rect class="literal" x="5" y="35" width="68" height="25" rx="7"/><text class="text" x="15" y="52">UPDATE</text><a xlink:href="../grammar_diagrams#table-name"><rect class="rule" x="83" y="35" width="91" height="25"/><text class="text" x="93" y="52">table_name</text></a><rect class="literal" x="184" y="35" width="43" height="25" rx="7"/><text class="text" x="194" y="52">SET</text><rect class="literal" x="289" y="5" width="24" height="25" rx="7"/><text class="text" x="299" y="22">,</text><a xlink:href="../grammar_diagrams#assignment"><rect class="rule" x="257" y="35" width="89" height="25"/><text class="text" x="267" y="52">assignment</text></a><rect class="literal" x="376" y="35" width="65" height="25" rx="7"/><text class="text" x="386" y="52">WHERE</text><a xlink:href="../grammar_diagrams#where-expression"><rect class="rule" x="451" y="35" width="128" height="25"/><text class="text" x="461" y="52">where_expression</text></a></svg>

### assignment
```
assignment = ( column_name | column_name '[' index_expression ']' ) '=' expression;
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="492" height="65" viewbox="0 0 492 65"><path class="connector" d="M0 22h25m106 0h223m-344 0q5 0 5 5v20q0 5 5 5h5m106 0h10m25 0h10m123 0h10m25 0h5q5 0 5-5v-20q0-5 5-5m5 0h10m30 0h10m83 0h5"/><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="25" y="5" width="106" height="25"/><text class="text" x="35" y="22">column_name</text></a><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="25" y="35" width="106" height="25"/><text class="text" x="35" y="52">column_name</text></a><rect class="literal" x="141" y="35" width="25" height="25" rx="7"/><text class="text" x="151" y="52">[</text><a xlink:href="../grammar_diagrams#index-expression"><rect class="rule" x="176" y="35" width="123" height="25"/><text class="text" x="186" y="52">index_expression</text></a><rect class="literal" x="309" y="35" width="25" height="25" rx="7"/><text class="text" x="319" y="52">]</text><rect class="literal" x="364" y="5" width="30" height="25" rx="7"/><text class="text" x="374" y="22">=</text><a xlink:href="../grammar_diagrams#expression"><rect class="rule" x="404" y="5" width="83" height="25"/><text class="text" x="414" y="22">expression</text></a></svg>

## Literals

### database_name
```
database_name = '<Text Literal>';
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="117" height="35" viewbox="0 0 117 35"><path class="connector" d="M0 22h5m107 0h5"/><rect class="literal" x="5" y="5" width="107" height="25" rx="7"/><text class="text" x="15" y="22">&lt;Text Literal&gt;</text></svg>

### table_name
```
table_name = [ database_name '.' ] '<Text Literal>';
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="316" height="50" viewbox="0 0 316 50"><path class="connector" d="M0 22h25m115 0h10m24 0h20m-184 0q5 0 5 5v8q0 5 5 5h159q5 0 5-5v-8q0-5 5-5m5 0h10m107 0h5"/><a xlink:href="../grammar_diagrams#database-name"><rect class="rule" x="25" y="5" width="115" height="25"/><text class="text" x="35" y="22">database_name</text></a><rect class="literal" x="150" y="5" width="24" height="25" rx="7"/><text class="text" x="160" y="22">.</text><rect class="literal" x="204" y="5" width="107" height="25" rx="7"/><text class="text" x="214" y="22">&lt;Text Literal&gt;</text></svg>

### column_name
```
column_name = '<Text Literal>';
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="117" height="35" viewbox="0 0 117 35"><path class="connector" d="M0 22h5m107 0h5"/><rect class="literal" x="5" y="5" width="107" height="25" rx="7"/><text class="text" x="15" y="22">&lt;Text Literal&gt;</text></svg>

### expression
```
expression = '<Expression>';
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="117" height="35" viewbox="0 0 117 35"><path class="connector" d="M0 22h5m107 0h5"/><rect class="literal" x="5" y="5" width="107" height="25" rx="7"/><text class="text" x="15" y="22">&lt;Expression&gt;</text></svg>

