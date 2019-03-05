---
title: Grammar Diagrams
description: Grammar Diagrams
summary: Diagrams of the grammar rules.
---

## DDL Statements

### create_database
```
create_database = 'CREATE' 'DATABASE' name;
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="235" height="35" viewbox="0 0 235 35"><path class="connector" d="M0 22h5m67 0h10m84 0h10m54 0h5"/><rect class="literal" x="5" y="5" width="67" height="25" rx="7"/><text class="text" x="15" y="22">CREATE</text><rect class="literal" x="82" y="5" width="84" height="25" rx="7"/><text class="text" x="92" y="22">DATABASE</text><a xlink:href="../grammar_diagrams#name"><rect class="rule" x="176" y="5" width="54" height="25"/><text class="text" x="186" y="22">name</text></a></svg>

### drop_database
```
drop_database = 'DROP' 'DATABASE' name;
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="221" height="35" viewbox="0 0 221 35"><path class="connector" d="M0 22h5m53 0h10m84 0h10m54 0h5"/><rect class="literal" x="5" y="5" width="53" height="25" rx="7"/><text class="text" x="15" y="22">DROP</text><rect class="literal" x="68" y="5" width="84" height="25" rx="7"/><text class="text" x="78" y="22">DATABASE</text><a xlink:href="../grammar_diagrams#name"><rect class="rule" x="162" y="5" width="54" height="25"/><text class="text" x="172" y="22">name</text></a></svg>

### create_table
```
create_table = 'CREATE' 'TABLE' qualified_name '(' table_element { ',' table_element } ')';
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="492" height="65" viewbox="0 0 492 65"><path class="connector" d="M0 52h5m67 0h10m58 0h10m111 0h10m25 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h46m24 0h46q5 0 5 5v20q0 5-5 5m-5 0h30m25 0h5"/><rect class="literal" x="5" y="35" width="67" height="25" rx="7"/><text class="text" x="15" y="52">CREATE</text><rect class="literal" x="82" y="35" width="58" height="25" rx="7"/><text class="text" x="92" y="52">TABLE</text><a xlink:href="../grammar_diagrams#qualified-name"><rect class="rule" x="150" y="35" width="111" height="25"/><text class="text" x="160" y="52">qualified_name</text></a><rect class="literal" x="271" y="35" width="25" height="25" rx="7"/><text class="text" x="281" y="52">(</text><rect class="literal" x="367" y="5" width="24" height="25" rx="7"/><text class="text" x="377" y="22">,</text><a xlink:href="../grammar_diagrams#table-element"><rect class="rule" x="326" y="35" width="106" height="25"/><text class="text" x="336" y="52">table_element</text></a><rect class="literal" x="462" y="35" width="25" height="25" rx="7"/><text class="text" x="472" y="52">)</text></svg>

### table_element
```
table_element = table_column | table_constraints;
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="173" height="65" viewbox="0 0 173 65"><path class="connector" d="M0 22h25m101 0h42m-158 0q5 0 5 5v20q0 5 5 5h5m123 0h5q5 0 5-5v-20q0-5 5-5m5 0h5"/><a xlink:href="../grammar_diagrams#table-column"><rect class="rule" x="25" y="5" width="101" height="25"/><text class="text" x="35" y="22">table_column</text></a><a xlink:href="../grammar_diagrams#table-constraints"><rect class="rule" x="25" y="35" width="123" height="25"/><text class="text" x="35" y="52">table_constraints</text></a></svg>

### table_column
```
table_column = column_name column_type { column_constraint | column_default_value };
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="507" height="95" viewbox="0 0 507 95"><path class="connector" d="M0 37h5m106 0h10m98 0h50m-5 0q-5 0-5-5v-17q0-5 5-5h203q5 0 5 5v17q0 5-5 5m-198 0h20m132 0h41m-188 0q5 0 5 5v20q0 5 5 5h5m153 0h5q5 0 5-5v-20q0-5 5-5m5 0h40m-268 0q5 0 5 5v38q0 5 5 5h243q5 0 5-5v-38q0-5 5-5m5 0h5"/><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="5" y="20" width="106" height="25"/><text class="text" x="15" y="37">column_name</text></a><a xlink:href="../grammar_diagrams#column-type"><rect class="rule" x="121" y="20" width="98" height="25"/><text class="text" x="131" y="37">column_type</text></a><a xlink:href="../grammar_diagrams#column-constraint"><rect class="rule" x="289" y="20" width="132" height="25"/><text class="text" x="299" y="37">column_constraint</text></a><a xlink:href="../grammar_diagrams#column-default-value"><rect class="rule" x="289" y="50" width="153" height="25"/><text class="text" x="299" y="67">column_default_value</text></a></svg>

### column_constraint
```
column_constraint = 'PRIMARY' 'KEY' | 'NOT' 'NULL' | 'CHECK' '(' expression ')';
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="274" height="95" viewbox="0 0 274 95"><path class="connector" d="M0 22h25m73 0h10m43 0h118m-254 25q0 5 5 5h5m45 0h10m52 0h122q5 0 5-5m-249-25q5 0 5 5v50q0 5 5 5h5m61 0h10m25 0h10m83 0h10m25 0h5q5 0 5-5v-50q0-5 5-5m5 0h5"/><rect class="literal" x="25" y="5" width="73" height="25" rx="7"/><text class="text" x="35" y="22">PRIMARY</text><rect class="literal" x="108" y="5" width="43" height="25" rx="7"/><text class="text" x="118" y="22">KEY</text><rect class="literal" x="25" y="35" width="45" height="25" rx="7"/><text class="text" x="35" y="52">NOT</text><rect class="literal" x="80" y="35" width="52" height="25" rx="7"/><text class="text" x="90" y="52">NULL</text><rect class="literal" x="25" y="65" width="61" height="25" rx="7"/><text class="text" x="35" y="82">CHECK</text><rect class="literal" x="96" y="65" width="25" height="25" rx="7"/><text class="text" x="106" y="82">(</text><a xlink:href="../grammar_diagrams#expression"><rect class="rule" x="131" y="65" width="83" height="25"/><text class="text" x="141" y="82">expression</text></a><rect class="literal" x="224" y="65" width="25" height="25" rx="7"/><text class="text" x="234" y="82">)</text></svg>

### column_default_value
```
column_default_value = 'DEFAULT' expression;
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="178" height="35" viewbox="0 0 178 35"><path class="connector" d="M0 22h5m75 0h10m83 0h5"/><rect class="literal" x="5" y="5" width="75" height="25" rx="7"/><text class="text" x="15" y="22">DEFAULT</text><a xlink:href="../grammar_diagrams#expression"><rect class="rule" x="90" y="5" width="83" height="25"/><text class="text" x="100" y="22">expression</text></a></svg>

### table_constraints
```
table_constraints = 'PRIMARY' 'KEY' '(' column_list ')';
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="305" height="35" viewbox="0 0 305 35"><path class="connector" d="M0 22h5m73 0h10m43 0h10m25 0h10m89 0h10m25 0h5"/><rect class="literal" x="5" y="5" width="73" height="25" rx="7"/><text class="text" x="15" y="22">PRIMARY</text><rect class="literal" x="88" y="5" width="43" height="25" rx="7"/><text class="text" x="98" y="22">KEY</text><rect class="literal" x="141" y="5" width="25" height="25" rx="7"/><text class="text" x="151" y="22">(</text><a xlink:href="../grammar_diagrams#column-list"><rect class="rule" x="176" y="5" width="89" height="25"/><text class="text" x="186" y="22">column_list</text></a><rect class="literal" x="275" y="5" width="25" height="25" rx="7"/><text class="text" x="285" y="22">)</text></svg>

### column_list
```
column_list = name { ',' name };
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="104" height="65" viewbox="0 0 104 65"><path class="connector" d="M0 52h25m-5 0q-5 0-5-5v-20q0-5 5-5h20m24 0h20q5 0 5 5v20q0 5-5 5m-5 0h25"/><rect class="literal" x="40" y="5" width="24" height="25" rx="7"/><text class="text" x="50" y="22">,</text><a xlink:href="../grammar_diagrams#name"><rect class="rule" x="25" y="35" width="54" height="25"/><text class="text" x="35" y="52">name</text></a></svg>

### drop_table
```
drop_table = 'DROP' 'TABLE' qualified_name;
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="252" height="35" viewbox="0 0 252 35"><path class="connector" d="M0 22h5m53 0h10m58 0h10m111 0h5"/><rect class="literal" x="5" y="5" width="53" height="25" rx="7"/><text class="text" x="15" y="22">DROP</text><rect class="literal" x="68" y="5" width="58" height="25" rx="7"/><text class="text" x="78" y="22">TABLE</text><a xlink:href="../grammar_diagrams#qualified-name"><rect class="rule" x="136" y="5" width="111" height="25"/><text class="text" x="146" y="22">qualified_name</text></a></svg>

### create_view
```
create_view = 'CREATE' [ 'OR' 'REPLACE' ] 'VIEW' qualified_name [ '(' column_list ')' ] 'AS' select;
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="751" height="50" viewbox="0 0 751 50"><path class="connector" d="M0 22h5m67 0h30m37 0h10m74 0h20m-156 0q5 0 5 5v8q0 5 5 5h131q5 0 5-5v-8q0-5 5-5m5 0h10m53 0h10m111 0h30m25 0h10m89 0h10m25 0h20m-194 0q5 0 5 5v8q0 5 5 5h169q5 0 5-5v-8q0-5 5-5m5 0h10m36 0h10m54 0h5"/><rect class="literal" x="5" y="5" width="67" height="25" rx="7"/><text class="text" x="15" y="22">CREATE</text><rect class="literal" x="102" y="5" width="37" height="25" rx="7"/><text class="text" x="112" y="22">OR</text><rect class="literal" x="149" y="5" width="74" height="25" rx="7"/><text class="text" x="159" y="22">REPLACE</text><rect class="literal" x="253" y="5" width="53" height="25" rx="7"/><text class="text" x="263" y="22">VIEW</text><a xlink:href="../grammar_diagrams#qualified-name"><rect class="rule" x="316" y="5" width="111" height="25"/><text class="text" x="326" y="22">qualified_name</text></a><rect class="literal" x="457" y="5" width="25" height="25" rx="7"/><text class="text" x="467" y="22">(</text><a xlink:href="../grammar_diagrams#column-list"><rect class="rule" x="492" y="5" width="89" height="25"/><text class="text" x="502" y="22">column_list</text></a><rect class="literal" x="591" y="5" width="25" height="25" rx="7"/><text class="text" x="601" y="22">)</text><rect class="literal" x="646" y="5" width="36" height="25" rx="7"/><text class="text" x="656" y="22">AS</text><a xlink:href="../grammar_diagrams#select"><rect class="rule" x="692" y="5" width="54" height="25"/><text class="text" x="702" y="22">select</text></a></svg>

### create_user
```
create_user = 'CREATE' 'USER' name;
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="204" height="35" viewbox="0 0 204 35"><path class="connector" d="M0 22h5m67 0h10m53 0h10m54 0h5"/><rect class="literal" x="5" y="5" width="67" height="25" rx="7"/><text class="text" x="15" y="22">CREATE</text><rect class="literal" x="82" y="5" width="53" height="25" rx="7"/><text class="text" x="92" y="22">USER</text><a xlink:href="../grammar_diagrams#name"><rect class="rule" x="145" y="5" width="54" height="25"/><text class="text" x="155" y="22">name</text></a></svg>

### grant
```
grant = 'GRANT' privileges 'ON' privilege_target 'TO' name { ',' name } [ 'WITH' 'GRANT' 'OPTION' ];
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="727" height="80" viewbox="0 0 727 80"><path class="connector" d="M0 52h5m61 0h10m75 0h10m38 0h10m113 0h10m36 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h20m24 0h20q5 0 5 5v20q0 5-5 5m-5 0h50m53 0h10m61 0h10m66 0h20m-235 0q5 0 5 5v8q0 5 5 5h210q5 0 5-5v-8q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="35" width="61" height="25" rx="7"/><text class="text" x="15" y="52">GRANT</text><a xlink:href="../grammar_diagrams#privileges"><rect class="rule" x="76" y="35" width="75" height="25"/><text class="text" x="86" y="52">privileges</text></a><rect class="literal" x="161" y="35" width="38" height="25" rx="7"/><text class="text" x="171" y="52">ON</text><a xlink:href="../grammar_diagrams#privilege-target"><rect class="rule" x="209" y="35" width="113" height="25"/><text class="text" x="219" y="52">privilege_target</text></a><rect class="literal" x="332" y="35" width="36" height="25" rx="7"/><text class="text" x="342" y="52">TO</text><rect class="literal" x="413" y="5" width="24" height="25" rx="7"/><text class="text" x="423" y="22">,</text><a xlink:href="../grammar_diagrams#name"><rect class="rule" x="398" y="35" width="54" height="25"/><text class="text" x="408" y="52">name</text></a><rect class="literal" x="502" y="35" width="53" height="25" rx="7"/><text class="text" x="512" y="52">WITH</text><rect class="literal" x="565" y="35" width="61" height="25" rx="7"/><text class="text" x="575" y="52">GRANT</text><rect class="literal" x="636" y="35" width="66" height="25" rx="7"/><text class="text" x="646" y="52">OPTION</text></svg>

### revoke
```
revoke = 'REVOKE' privileges 'ON' privilege_target 'FROM' name { ',' name } [ 'CASCADE' | 'RESTRICT' ];
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="632" height="100" viewbox="0 0 632 100"><path class="connector" d="M0 52h5m69 0h10m75 0h10m38 0h10m113 0h10m54 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h20m24 0h20q5 0 5 5v20q0 5-5 5m-5 0h50m77 0h22m-109 25q0 5 5 5h5m79 0h5q5 0 5-5m-104-25q5 0 5 5v33q0 5 5 5h89q5 0 5-5v-33q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="35" width="69" height="25" rx="7"/><text class="text" x="15" y="52">REVOKE</text><a xlink:href="../grammar_diagrams#privileges"><rect class="rule" x="84" y="35" width="75" height="25"/><text class="text" x="94" y="52">privileges</text></a><rect class="literal" x="169" y="35" width="38" height="25" rx="7"/><text class="text" x="179" y="52">ON</text><a xlink:href="../grammar_diagrams#privilege-target"><rect class="rule" x="217" y="35" width="113" height="25"/><text class="text" x="227" y="52">privilege_target</text></a><rect class="literal" x="340" y="35" width="54" height="25" rx="7"/><text class="text" x="350" y="52">FROM</text><rect class="literal" x="439" y="5" width="24" height="25" rx="7"/><text class="text" x="449" y="22">,</text><a xlink:href="../grammar_diagrams#name"><rect class="rule" x="424" y="35" width="54" height="25"/><text class="text" x="434" y="52">name</text></a><rect class="literal" x="528" y="35" width="77" height="25" rx="7"/><text class="text" x="538" y="52">CASCADE</text><rect class="literal" x="528" y="65" width="79" height="25" rx="7"/><text class="text" x="538" y="82">RESTRICT</text></svg>

## DML Statements

### insert
```
insert = 'INSERT' 'INTO' qualified_name [ 'AS' name ] '(' column_list ')' 'VALUES' values_list { ',' values_list };
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="786" height="80" viewbox="0 0 786 80"><path class="connector" d="M0 52h5m65 0h10m50 0h10m111 0h30m36 0h10m54 0h20m-135 0q5 0 5 5v8q0 5 5 5h110q5 0 5-5v-8q0-5 5-5m5 0h10m25 0h10m89 0h10m25 0h10m68 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h34m24 0h35q5 0 5 5v20q0 5-5 5m-5 0h25"/><rect class="literal" x="5" y="35" width="65" height="25" rx="7"/><text class="text" x="15" y="52">INSERT</text><rect class="literal" x="80" y="35" width="50" height="25" rx="7"/><text class="text" x="90" y="52">INTO</text><a xlink:href="../grammar_diagrams#qualified-name"><rect class="rule" x="140" y="35" width="111" height="25"/><text class="text" x="150" y="52">qualified_name</text></a><rect class="literal" x="281" y="35" width="36" height="25" rx="7"/><text class="text" x="291" y="52">AS</text><a xlink:href="../grammar_diagrams#name"><rect class="rule" x="327" y="35" width="54" height="25"/><text class="text" x="337" y="52">name</text></a><rect class="literal" x="411" y="35" width="25" height="25" rx="7"/><text class="text" x="421" y="52">(</text><a xlink:href="../grammar_diagrams#column-list"><rect class="rule" x="446" y="35" width="89" height="25"/><text class="text" x="456" y="52">column_list</text></a><rect class="literal" x="545" y="35" width="25" height="25" rx="7"/><text class="text" x="555" y="52">)</text><rect class="literal" x="580" y="35" width="68" height="25" rx="7"/><text class="text" x="590" y="52">VALUES</text><rect class="literal" x="707" y="5" width="24" height="25" rx="7"/><text class="text" x="717" y="22">,</text><a xlink:href="../grammar_diagrams#values-list"><rect class="rule" x="678" y="35" width="83" height="25"/><text class="text" x="688" y="52">values_list</text></a></svg>

### values_list
```
values_list = '(' expression { ',' expression } ')';
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="203" height="65" viewbox="0 0 203 65"><path class="connector" d="M0 52h5m25 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h34m24 0h35q5 0 5 5v20q0 5-5 5m-5 0h30m25 0h5"/><rect class="literal" x="5" y="35" width="25" height="25" rx="7"/><text class="text" x="15" y="52">(</text><rect class="literal" x="89" y="5" width="24" height="25" rx="7"/><text class="text" x="99" y="22">,</text><a xlink:href="../grammar_diagrams#expression"><rect class="rule" x="60" y="35" width="83" height="25"/><text class="text" x="70" y="52">expression</text></a><rect class="literal" x="173" y="35" width="25" height="25" rx="7"/><text class="text" x="183" y="52">)</text></svg>

### select
```
select = [ 'WITH' [ 'RECURSIVE' ] with_query { ',' with_query } ] \ 'SELECT' [ 'ALL' | 'DISTINCT' [ ON '(' expression { ',' expression } ')' ] ] \ [ '*' | expression [ [ 'AS' ] name ] { ',' name } ] \ [ 'FROM' from_item { ',' from_item } ] \ [ 'WHERE' condition ] \ [ 'GROUP' 'BY' grouping_element { ',' grouping_element } ] \ [ 'HAVING' condition { ',' condition } ] \ [ ( 'UNION' | 'INTERSECT' | 'EXCEPT' ) [ 'ALL' | 'DISTINCT' ] select ] \ [ 'ORDER' 'BY' order_expr { ',' order_expr } ] \ [ 'LIMIT' [ integer | 'ALL' ] ] \ [ 'OFFSET' integer [ 'ROW' | 'ROWS' ] ];
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="501" height="975" viewbox="0 0 501 975"><path class="connector" d="M0 52h25m53 0h30m90 0h20m-125 0q5 0 5 5v8q0 5 5 5h100q5 0 5-5v-8q0-5 5-5m5 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h37m24 0h37q5 0 5 5v20q0 5-5 5m-5 0h40m-366 0q5 0 5 5v23q0 5 5 5h341q5 0 5-5v-23q0-5 5-5m5 0h5m-381 65h5m66 0h30m42 0h347m-399 55q0 5 5 5h5m78 0h30m38 0h10m25 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h34m24 0h35q5 0 5 5v20q0 5-5 5m-5 0h30m25 0h20m-276 0q5 0 5 5v8q0 5 5 5h251q5 0 5-5v-8q0-5 5-5m5 0h5q5 0 5-5m-394-55q5 0 5 5v78q0 5 5 5h379q5 0 5-5v-78q0-5 5-5m5 0h5m-495 115h25m28 0h443m-481 40q0 5 5 5h5m83 0h50m36 0h20m-71 0q5 0 5 5v8q0 5 5 5h46q5 0 5-5v-8q0-5 5-5m5 0h10m54 0h20m-175 0q5 0 5 5v23q0 5 5 5h150q5 0 5-5v-23q0-5 5-5m5 0h50m-5 0q-5 0-5-5v-17q0-5 5-5h98q5 0 5 5v17q0 5-5 5m-69 0h10m54 0h40m-163 0q5 0 5 5v8q0 5 5 5h138q5 0 5-5v-8q0-5 5-5m5 0h5q5 0 5-5m-476-40q5 0 5 5v78q0 5 5 5h461q5 0 5-5v-78q0-5 5-5m5 0h5m-501 145h25m54 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h34m24 0h35q5 0 5 5v20q0 5-5 5m-5 0h40m-222 0q5 0 5 5v8q0 5 5 5h197q5 0 5-5v-8q0-5 5-5m5 0h5m-237 50h25m65 0h10m74 0h20m-184 0q5 0 5 5v8q0 5 5 5h159q5 0 5-5v-8q0-5 5-5m5 0h5m-199 80h25m62 0h10m35 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h57m24 0h58q5 0 5 5v20q0 5-5 5m-5 0h40m-321 0q5 0 5 5v8q0 5 5 5h296q5 0 5-5v-8q0-5 5-5m5 0h5m-336 80h25m68 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h30m24 0h30q5 0 5 5v20q0 5-5 5m-5 0h40m-227 0q5 0 5 5v8q0 5 5 5h202q5 0 5-5v-8q0-5 5-5m5 0h5m-242 50h45m61 0h47m-118 25q0 5 5 5h5m88 0h5q5 0 5-5m-113-25q5 0 5 5v50q0 5 5 5h5m66 0h27q5 0 5-5v-50q0-5 5-5m5 0h30m42 0h56m-108 25q0 5 5 5h5m78 0h5q5 0 5-5m-103-25q5 0 5 5v33q0 5 5 5h88q5 0 5-5v-33q0-5 5-5m5 0h10m54 0h20m-355 0q5 0 5 5v68q0 5 5 5h330q5 0 5-5v-68q0-5 5-5m5 0h5m-370 140h25m62 0h10m35 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h35m24 0h36q5 0 5 5v20q0 5-5 5m-5 0h40m-277 0q5 0 5 5v8q0 5 5 5h252q5 0 5-5v-8q0-5 5-5m5 0h5m-292 50h25m54 0h30m62 0h20m-92 25q0 5 5 5h5m42 0h25q5 0 5-5m-87-25q5 0 5 5v33q0 5 5 5h72q5 0 5-5v-33q0-5 5-5m5 0h20m-201 0q5 0 5 5v43q0 5 5 5h176q5 0 5-5v-43q0-5 5-5m5 0h5m-216 85h25m66 0h10m62 0h30m49 0h28m-87 25q0 5 5 5h5m57 0h5q5 0 5-5m-82-25q5 0 5 5v33q0 5 5 5h67q5 0 5-5v-33q0-5 5-5m5 0h20m-280 0q5 0 5 5v43q0 5 5 5h255q5 0 5-5v-43q0-5 5-5m5 0h5"/><rect class="literal" x="25" y="35" width="53" height="25" rx="7"/><text class="text" x="35" y="52">WITH</text><rect class="literal" x="108" y="35" width="90" height="25" rx="7"/><text class="text" x="118" y="52">RECURSIVE</text><rect class="literal" x="280" y="5" width="24" height="25" rx="7"/><text class="text" x="290" y="22">,</text><a xlink:href="../grammar_diagrams#with-query"><rect class="rule" x="248" y="35" width="88" height="25"/><text class="text" x="258" y="52">with_query</text></a><rect class="literal" x="5" y="100" width="66" height="25" rx="7"/><text class="text" x="15" y="117">SELECT</text><rect class="literal" x="101" y="100" width="42" height="25" rx="7"/><text class="text" x="111" y="117">ALL</text><rect class="literal" x="101" y="160" width="78" height="25" rx="7"/><text class="text" x="111" y="177">DISTINCT</text><a xlink:href="../grammar_diagrams#ON"><rect class="rule" x="209" y="160" width="38" height="25"/><text class="text" x="219" y="177">ON</text></a><rect class="literal" x="257" y="160" width="25" height="25" rx="7"/><text class="text" x="267" y="177">(</text><rect class="literal" x="341" y="130" width="24" height="25" rx="7"/><text class="text" x="351" y="147">,</text><a xlink:href="../grammar_diagrams#expression"><rect class="rule" x="312" y="160" width="83" height="25"/><text class="text" x="322" y="177">expression</text></a><rect class="literal" x="425" y="160" width="25" height="25" rx="7"/><text class="text" x="435" y="177">)</text><rect class="literal" x="25" y="215" width="28" height="25" rx="7"/><text class="text" x="35" y="232">*</text><a xlink:href="../grammar_diagrams#expression"><rect class="rule" x="25" y="260" width="83" height="25"/><text class="text" x="35" y="277">expression</text></a><rect class="literal" x="158" y="260" width="36" height="25" rx="7"/><text class="text" x="168" y="277">AS</text><a xlink:href="../grammar_diagrams#name"><rect class="rule" x="224" y="260" width="54" height="25"/><text class="text" x="234" y="277">name</text></a><rect class="literal" x="348" y="260" width="24" height="25" rx="7"/><text class="text" x="358" y="277">,</text><a xlink:href="../grammar_diagrams#name"><rect class="rule" x="382" y="260" width="54" height="25"/><text class="text" x="392" y="277">name</text></a><rect class="literal" x="25" y="360" width="54" height="25" rx="7"/><text class="text" x="35" y="377">FROM</text><rect class="literal" x="138" y="330" width="24" height="25" rx="7"/><text class="text" x="148" y="347">,</text><a xlink:href="../grammar_diagrams#from-item"><rect class="rule" x="109" y="360" width="83" height="25"/><text class="text" x="119" y="377">from_item</text></a><rect class="literal" x="25" y="410" width="65" height="25" rx="7"/><text class="text" x="35" y="427">WHERE</text><a xlink:href="../grammar_diagrams#condition"><rect class="rule" x="100" y="410" width="74" height="25"/><text class="text" x="110" y="427">condition</text></a><rect class="literal" x="25" y="490" width="62" height="25" rx="7"/><text class="text" x="35" y="507">GROUP</text><rect class="literal" x="97" y="490" width="35" height="25" rx="7"/><text class="text" x="107" y="507">BY</text><rect class="literal" x="214" y="460" width="24" height="25" rx="7"/><text class="text" x="224" y="477">,</text><a xlink:href="../grammar_diagrams#grouping-element"><rect class="rule" x="162" y="490" width="129" height="25"/><text class="text" x="172" y="507">grouping_element</text></a><rect class="literal" x="25" y="570" width="68" height="25" rx="7"/><text class="text" x="35" y="587">HAVING</text><rect class="literal" x="148" y="540" width="24" height="25" rx="7"/><text class="text" x="158" y="557">,</text><a xlink:href="../grammar_diagrams#condition"><rect class="rule" x="123" y="570" width="74" height="25"/><text class="text" x="133" y="587">condition</text></a><rect class="literal" x="45" y="620" width="61" height="25" rx="7"/><text class="text" x="55" y="637">UNION</text><rect class="literal" x="45" y="650" width="88" height="25" rx="7"/><text class="text" x="55" y="667">INTERSECT</text><rect class="literal" x="45" y="680" width="66" height="25" rx="7"/><text class="text" x="55" y="697">EXCEPT</text><rect class="literal" x="183" y="620" width="42" height="25" rx="7"/><text class="text" x="193" y="637">ALL</text><rect class="literal" x="183" y="650" width="78" height="25" rx="7"/><text class="text" x="193" y="667">DISTINCT</text><a xlink:href="../grammar_diagrams#select"><rect class="rule" x="291" y="620" width="54" height="25"/><text class="text" x="301" y="637">select</text></a><rect class="literal" x="25" y="760" width="62" height="25" rx="7"/><text class="text" x="35" y="777">ORDER</text><rect class="literal" x="97" y="760" width="35" height="25" rx="7"/><text class="text" x="107" y="777">BY</text><rect class="literal" x="192" y="730" width="24" height="25" rx="7"/><text class="text" x="202" y="747">,</text><a xlink:href="../grammar_diagrams#order-expr"><rect class="rule" x="162" y="760" width="85" height="25"/><text class="text" x="172" y="777">order_expr</text></a><rect class="literal" x="25" y="810" width="54" height="25" rx="7"/><text class="text" x="35" y="827">LIMIT</text><a xlink:href="../grammar_diagrams#integer"><rect class="rule" x="109" y="810" width="62" height="25"/><text class="text" x="119" y="827">integer</text></a><rect class="literal" x="109" y="840" width="42" height="25" rx="7"/><text class="text" x="119" y="857">ALL</text><rect class="literal" x="25" y="895" width="66" height="25" rx="7"/><text class="text" x="35" y="912">OFFSET</text><a xlink:href="../grammar_diagrams#integer"><rect class="rule" x="101" y="895" width="62" height="25"/><text class="text" x="111" y="912">integer</text></a><rect class="literal" x="193" y="895" width="49" height="25" rx="7"/><text class="text" x="203" y="912">ROW</text><rect class="literal" x="193" y="925" width="57" height="25" rx="7"/><text class="text" x="203" y="942">ROWS</text></svg>

### order_expr
```
order_expr = expression [ 'ASC' | 'DESC' | 'USING' operator ] [ 'NULLS' ( 'FIRST' | 'LAST' ) ];
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="498" height="100" viewbox="0 0 498 100"><path class="connector" d="M0 22h5m83 0h30m44 0h116m-170 25q0 5 5 5h5m53 0h92q5 0 5-5m-160 30q0 5 5 5h5m60 0h10m70 0h5q5 0 5-5m-165-55q5 0 5 5v63q0 5 5 5h150q5 0 5-5v-63q0-5 5-5m5 0h30m60 0h30m55 0h20m-90 0q5 0 5 5v20q0 5 5 5h5m50 0h10q5 0 5-5v-20q0-5 5-5m5 0h20m-200 0q5 0 5 5v38q0 5 5 5h175q5 0 5-5v-38q0-5 5-5m5 0h5"/><a xlink:href="../grammar_diagrams#expression"><rect class="rule" x="5" y="5" width="83" height="25"/><text class="text" x="15" y="22">expression</text></a><rect class="literal" x="118" y="5" width="44" height="25" rx="7"/><text class="text" x="128" y="22">ASC</text><rect class="literal" x="118" y="35" width="53" height="25" rx="7"/><text class="text" x="128" y="52">DESC</text><rect class="literal" x="118" y="65" width="60" height="25" rx="7"/><text class="text" x="128" y="82">USING</text><a xlink:href="../grammar_diagrams#operator"><rect class="rule" x="188" y="65" width="70" height="25"/><text class="text" x="198" y="82">operator</text></a><rect class="literal" x="308" y="5" width="60" height="25" rx="7"/><text class="text" x="318" y="22">NULLS</text><rect class="literal" x="398" y="5" width="55" height="25" rx="7"/><text class="text" x="408" y="22">FIRST</text><rect class="literal" x="398" y="35" width="50" height="25" rx="7"/><text class="text" x="408" y="52">LAST</text></svg>

### grouping_element
```
grouping_element = 'grouping_element';
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="139" height="35" viewbox="0 0 139 35"><path class="connector" d="M0 22h5m129 0h5"/><rect class="literal" x="5" y="5" width="129" height="25" rx="7"/><text class="text" x="15" y="22">grouping_element</text></svg>

## Transactions

### transaction
```
transaction = ( 'ABORT' | 'ROLLBACK' | 'BEGIN' | 'END' | 'COMMIT' ) [ 'TRANSACTION' | 'WORK' ];
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="289" height="155" viewbox="0 0 289 155"><path class="connector" d="M0 22h25m60 0h43m-113 25q0 5 5 5h5m83 0h5q5 0 5-5m-103 30q0 5 5 5h5m59 0h29q5 0 5-5m-103 30q0 5 5 5h5m46 0h42q5 0 5-5m-108-85q5 0 5 5v110q0 5 5 5h5m69 0h19q5 0 5-5v-110q0-5 5-5m5 0h30m106 0h20m-136 25q0 5 5 5h5m57 0h54q5 0 5-5m-131-25q5 0 5 5v33q0 5 5 5h116q5 0 5-5v-33q0-5 5-5m5 0h5"/><rect class="literal" x="25" y="5" width="60" height="25" rx="7"/><text class="text" x="35" y="22">ABORT</text><rect class="literal" x="25" y="35" width="83" height="25" rx="7"/><text class="text" x="35" y="52">ROLLBACK</text><rect class="literal" x="25" y="65" width="59" height="25" rx="7"/><text class="text" x="35" y="82">BEGIN</text><rect class="literal" x="25" y="95" width="46" height="25" rx="7"/><text class="text" x="35" y="112">END</text><rect class="literal" x="25" y="125" width="69" height="25" rx="7"/><text class="text" x="35" y="142">COMMIT</text><rect class="literal" x="158" y="5" width="106" height="25" rx="7"/><text class="text" x="168" y="22">TRANSACTION</text><rect class="literal" x="158" y="35" width="57" height="25" rx="7"/><text class="text" x="168" y="52">WORK</text></svg>

### set
```
set = 'SET' 'TRANSACTION' 'ISOLATION' 'LEVEL' ( 'READ' 'UNCOMMITTED' | 'READ' 'COMMITTED' | 'REPEATABLE' 'READ' );
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="551" height="95" viewbox="0 0 551 95"><path class="connector" d="M0 22h5m43 0h10m106 0h10m87 0h10m58 0h30m53 0h10m104 0h20m-197 25q0 5 5 5h5m53 0h10m86 0h23q5 0 5-5m-192-25q5 0 5 5v50q0 5 5 5h5m97 0h10m53 0h12q5 0 5-5v-50q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="5" width="43" height="25" rx="7"/><text class="text" x="15" y="22">SET</text><rect class="literal" x="58" y="5" width="106" height="25" rx="7"/><text class="text" x="68" y="22">TRANSACTION</text><rect class="literal" x="174" y="5" width="87" height="25" rx="7"/><text class="text" x="184" y="22">ISOLATION</text><rect class="literal" x="271" y="5" width="58" height="25" rx="7"/><text class="text" x="281" y="22">LEVEL</text><rect class="literal" x="359" y="5" width="53" height="25" rx="7"/><text class="text" x="369" y="22">READ</text><rect class="literal" x="422" y="5" width="104" height="25" rx="7"/><text class="text" x="432" y="22">UNCOMMITTED</text><rect class="literal" x="359" y="35" width="53" height="25" rx="7"/><text class="text" x="369" y="52">READ</text><rect class="literal" x="422" y="35" width="86" height="25" rx="7"/><text class="text" x="432" y="52">COMMITTED</text><rect class="literal" x="359" y="65" width="97" height="25" rx="7"/><text class="text" x="369" y="82">REPEATABLE</text><rect class="literal" x="466" y="65" width="53" height="25" rx="7"/><text class="text" x="476" y="82">READ</text></svg>

### show
```
show = 'SHOW' 'TRANSACTION' 'ISOLATION' 'LEVEL';
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="349" height="35" viewbox="0 0 349 35"><path class="connector" d="M0 22h5m58 0h10m106 0h10m87 0h10m58 0h5"/><rect class="literal" x="5" y="5" width="58" height="25" rx="7"/><text class="text" x="15" y="22">SHOW</text><rect class="literal" x="73" y="5" width="106" height="25" rx="7"/><text class="text" x="83" y="22">TRANSACTION</text><rect class="literal" x="189" y="5" width="87" height="25" rx="7"/><text class="text" x="199" y="22">ISOLATION</text><rect class="literal" x="286" y="5" width="58" height="25" rx="7"/><text class="text" x="296" y="22">LEVEL</text></svg>

## Prepared Statements

### prepare
```
prepare = 'PREPARE' name [ '(' data_type { ',' data_type } ')' ] 'AS' statement;
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="526" height="80" viewbox="0 0 526 80"><path class="connector" d="M0 52h5m74 0h10m54 0h30m25 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h33m24 0h33q5 0 5 5v20q0 5-5 5m-5 0h30m25 0h20m-225 0q5 0 5 5v8q0 5 5 5h200q5 0 5-5v-8q0-5 5-5m5 0h10m36 0h10m82 0h5"/><rect class="literal" x="5" y="35" width="74" height="25" rx="7"/><text class="text" x="15" y="52">PREPARE</text><a xlink:href="../grammar_diagrams#name"><rect class="rule" x="89" y="35" width="54" height="25"/><text class="text" x="99" y="52">name</text></a><rect class="literal" x="173" y="35" width="25" height="25" rx="7"/><text class="text" x="183" y="52">(</text><rect class="literal" x="256" y="5" width="24" height="25" rx="7"/><text class="text" x="266" y="22">,</text><a xlink:href="../grammar_diagrams#data-type"><rect class="rule" x="228" y="35" width="80" height="25"/><text class="text" x="238" y="52">data_type</text></a><rect class="literal" x="338" y="35" width="25" height="25" rx="7"/><text class="text" x="348" y="52">)</text><rect class="literal" x="393" y="35" width="36" height="25" rx="7"/><text class="text" x="403" y="52">AS</text><a xlink:href="../grammar_diagrams#statement"><rect class="rule" x="439" y="35" width="82" height="25"/><text class="text" x="449" y="52">statement</text></a></svg>

### execute
```
execute = 'EXECUTE' name [ '(' expression { ',' expression } ')' ];
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="393" height="80" viewbox="0 0 393 80"><path class="connector" d="M0 52h5m76 0h10m54 0h30m25 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h34m24 0h35q5 0 5 5v20q0 5-5 5m-5 0h30m25 0h20m-228 0q5 0 5 5v8q0 5 5 5h203q5 0 5-5v-8q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="35" width="76" height="25" rx="7"/><text class="text" x="15" y="52">EXECUTE</text><a xlink:href="../grammar_diagrams#name"><rect class="rule" x="91" y="35" width="54" height="25"/><text class="text" x="101" y="52">name</text></a><rect class="literal" x="175" y="35" width="25" height="25" rx="7"/><text class="text" x="185" y="52">(</text><rect class="literal" x="259" y="5" width="24" height="25" rx="7"/><text class="text" x="269" y="22">,</text><a xlink:href="../grammar_diagrams#expression"><rect class="rule" x="230" y="35" width="83" height="25"/><text class="text" x="240" y="52">expression</text></a><rect class="literal" x="343" y="35" width="25" height="25" rx="7"/><text class="text" x="353" y="52">)</text></svg>

## Explain Statement

### explain
```
explain = 'EXPLAIN' ( [ 'ANALYZE' ] [ 'VERBOSE' ] | '(' option { ',' option } ')' ) statement;
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="466" height="110" viewbox="0 0 466 110"><path class="connector" d="M0 22h5m72 0h50m75 0h20m-110 0q5 0 5 5v8q0 5 5 5h85q5 0 5-5v-8q0-5 5-5m5 0h30m77 0h20m-112 0q5 0 5 5v8q0 5 5 5h87q5 0 5-5v-8q0-5 5-5m5 0h20m-277 0q5 0 5 5v65q0 5 5 5h5m25 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h21m24 0h22q5 0 5 5v20q0 5-5 5m-5 0h30m25 0h80q5 0 5-5v-65q0-5 5-5m5 0h10m82 0h5"/><rect class="literal" x="5" y="5" width="72" height="25" rx="7"/><text class="text" x="15" y="22">EXPLAIN</text><rect class="literal" x="127" y="5" width="75" height="25" rx="7"/><text class="text" x="137" y="22">ANALYZE</text><rect class="literal" x="252" y="5" width="77" height="25" rx="7"/><text class="text" x="262" y="22">VERBOSE</text><rect class="literal" x="107" y="80" width="25" height="25" rx="7"/><text class="text" x="117" y="97">(</text><rect class="literal" x="178" y="50" width="24" height="25" rx="7"/><text class="text" x="188" y="67">,</text><a xlink:href="../grammar_diagrams#option"><rect class="rule" x="162" y="80" width="57" height="25"/><text class="text" x="172" y="97">option</text></a><rect class="literal" x="249" y="80" width="25" height="25" rx="7"/><text class="text" x="259" y="97">)</text><a xlink:href="../grammar_diagrams#statement"><rect class="rule" x="379" y="5" width="82" height="25"/><text class="text" x="389" y="22">statement</text></a></svg>

### option
```
option = 'ANALYZE' [ boolean ] | 'VERBOSE' [ boolean ] | 'COSTS' [ boolean ] | 'BUFFERS' [ boolean ] | 'TIMING' [ boolean ] | 'SUMMARY' [ boolean ] | 'FORMAT' { 'TEXT' | 'XML' | 'JSON' | 'YAML' };
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="301" height="425" viewbox="0 0 301 425"><path class="connector" d="M0 22h25m75 0h30m66 0h20m-101 0q5 0 5 5v8q0 5 5 5h76q5 0 5-5v-8q0-5 5-5m5 0h80m-281 40q0 5 5 5h5m77 0h30m66 0h20m-101 0q5 0 5 5v8q0 5 5 5h76q5 0 5-5v-8q0-5 5-5m5 0h63q5 0 5-5m-271 45q0 5 5 5h5m60 0h30m66 0h20m-101 0q5 0 5 5v8q0 5 5 5h76q5 0 5-5v-8q0-5 5-5m5 0h80q5 0 5-5m-271 45q0 5 5 5h5m75 0h30m66 0h20m-101 0q5 0 5 5v8q0 5 5 5h76q5 0 5-5v-8q0-5 5-5m5 0h65q5 0 5-5m-271 45q0 5 5 5h5m65 0h30m66 0h20m-101 0q5 0 5 5v8q0 5 5 5h76q5 0 5-5v-8q0-5 5-5m5 0h75q5 0 5-5m-271 45q0 5 5 5h5m80 0h30m66 0h20m-101 0q5 0 5 5v8q0 5 5 5h76q5 0 5-5v-8q0-5 5-5m5 0h60q5 0 5-5m-276-220q5 0 5 5v275q0 5 5 5h5m69 0h50m-5 0q-5 0-5-5v-17q0-5 5-5h102q5 0 5 5v17q0 5-5 5m-97 0h20m50 0h22m-82 25q0 5 5 5h5m45 0h12q5 0 5-5m-72 30q0 5 5 5h5m51 0h6q5 0 5-5m-77-55q5 0 5 5v80q0 5 5 5h5m52 0h5q5 0 5-5v-80q0-5 5-5m5 0h40m-167 0q5 0 5 5v98q0 5 5 5h142q5 0 5-5v-98q0-5 5-5m5 0h5q5 0 5-5v-275q0-5 5-5m5 0h5"/><rect class="literal" x="25" y="5" width="75" height="25" rx="7"/><text class="text" x="35" y="22">ANALYZE</text><a xlink:href="../grammar_diagrams#boolean"><rect class="rule" x="130" y="5" width="66" height="25"/><text class="text" x="140" y="22">boolean</text></a><rect class="literal" x="25" y="50" width="77" height="25" rx="7"/><text class="text" x="35" y="67">VERBOSE</text><a xlink:href="../grammar_diagrams#boolean"><rect class="rule" x="132" y="50" width="66" height="25"/><text class="text" x="142" y="67">boolean</text></a><rect class="literal" x="25" y="95" width="60" height="25" rx="7"/><text class="text" x="35" y="112">COSTS</text><a xlink:href="../grammar_diagrams#boolean"><rect class="rule" x="115" y="95" width="66" height="25"/><text class="text" x="125" y="112">boolean</text></a><rect class="literal" x="25" y="140" width="75" height="25" rx="7"/><text class="text" x="35" y="157">BUFFERS</text><a xlink:href="../grammar_diagrams#boolean"><rect class="rule" x="130" y="140" width="66" height="25"/><text class="text" x="140" y="157">boolean</text></a><rect class="literal" x="25" y="185" width="65" height="25" rx="7"/><text class="text" x="35" y="202">TIMING</text><a xlink:href="../grammar_diagrams#boolean"><rect class="rule" x="120" y="185" width="66" height="25"/><text class="text" x="130" y="202">boolean</text></a><rect class="literal" x="25" y="230" width="80" height="25" rx="7"/><text class="text" x="35" y="247">SUMMARY</text><a xlink:href="../grammar_diagrams#boolean"><rect class="rule" x="135" y="230" width="66" height="25"/><text class="text" x="145" y="247">boolean</text></a><rect class="literal" x="25" y="290" width="69" height="25" rx="7"/><text class="text" x="35" y="307">FORMAT</text><rect class="literal" x="164" y="290" width="50" height="25" rx="7"/><text class="text" x="174" y="307">TEXT</text><rect class="literal" x="164" y="320" width="45" height="25" rx="7"/><text class="text" x="174" y="337">XML</text><rect class="literal" x="164" y="350" width="51" height="25" rx="7"/><text class="text" x="174" y="367">JSON</text><rect class="literal" x="164" y="380" width="52" height="25" rx="7"/><text class="text" x="174" y="397">YAML</text></svg>

## Literals

### boolean
```
boolean = 'TRUE' | 'FALSE';
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="108" height="65" viewbox="0 0 108 65"><path class="connector" d="M0 22h25m52 0h26m-93 0q5 0 5 5v20q0 5 5 5h5m58 0h5q5 0 5-5v-20q0-5 5-5m5 0h5"/><rect class="literal" x="25" y="5" width="52" height="25" rx="7"/><text class="text" x="35" y="22">TRUE</text><rect class="literal" x="25" y="35" width="58" height="25" rx="7"/><text class="text" x="35" y="52">FALSE</text></svg>

### name
```
name = '<Text Literal>';
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="117" height="35" viewbox="0 0 117 35"><path class="connector" d="M0 22h5m107 0h5"/><rect class="literal" x="5" y="5" width="107" height="25" rx="7"/><text class="text" x="15" y="22">&lt;Text Literal&gt;</text></svg>

### qualified_name
```
qualified_name = '<Text Literal>' { '.' '<Text Literal>' };
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="157" height="65" viewbox="0 0 157 65"><path class="connector" d="M0 52h25m-5 0q-5 0-5-5v-20q0-5 5-5h46m24 0h47q5 0 5 5v20q0 5-5 5m-5 0h25"/><rect class="literal" x="66" y="5" width="24" height="25" rx="7"/><text class="text" x="76" y="22">.</text><rect class="literal" x="25" y="35" width="107" height="25" rx="7"/><text class="text" x="35" y="52">&lt;Text Literal&gt;</text></svg>

### expression
```
expression = '<expression';
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="103" height="35" viewbox="0 0 103 35"><path class="connector" d="M0 22h5m93 0h5"/><rect class="literal" x="5" y="5" width="93" height="25" rx="7"/><text class="text" x="15" y="22">&lt;expression</text></svg>

### condition
```
condition = '<condition>';
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="104" height="35" viewbox="0 0 104 35"><path class="connector" d="M0 22h5m94 0h5"/><rect class="literal" x="5" y="5" width="94" height="25" rx="7"/><text class="text" x="15" y="22">&lt;condition&gt;</text></svg>

### create_sequence
```
create_sequence = 'CREATE' 'SEQUENCE' [ 'IF' 'NOT' 'EXISTS' ] sequence_name create_sequence_options;
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="695" height="50" viewbox="0 0 695 50"><path class="connector" d="M0 22h5m67 0h10m87 0h30m32 0h10m45 0h10m64 0h20m-196 0q5 0 5 5v8q0 5 5 5h171q5 0 5-5v-8q0-5 5-5m5 0h10m118 0h10m172 0h5"/><rect class="literal" x="5" y="5" width="67" height="25" rx="7"/><text class="text" x="15" y="22">CREATE</text><rect class="literal" x="82" y="5" width="87" height="25" rx="7"/><text class="text" x="92" y="22">SEQUENCE</text><rect class="literal" x="199" y="5" width="32" height="25" rx="7"/><text class="text" x="209" y="22">IF</text><rect class="literal" x="241" y="5" width="45" height="25" rx="7"/><text class="text" x="251" y="22">NOT</text><rect class="literal" x="296" y="5" width="64" height="25" rx="7"/><text class="text" x="306" y="22">EXISTS</text><a xlink:href="../grammar_diagrams#sequence-name"><rect class="rule" x="390" y="5" width="118" height="25"/><text class="text" x="400" y="22">sequence_name</text></a><a xlink:href="../grammar_diagrams#create-sequence-options"><rect class="rule" x="518" y="5" width="172" height="25"/><text class="text" x="528" y="22">create_sequence_options</text></a></svg>

### drop_sequence
```
drop_sequence = 'DROP' 'SEQUENCE' [ 'IF' 'EXISTS' ] sequence_name [ 'CASCADE' | 'RESTRICT' ];
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="573" height="70" viewbox="0 0 573 70"><path class="connector" d="M0 22h5m53 0h10m87 0h30m32 0h10m64 0h20m-141 0q5 0 5 5v8q0 5 5 5h116q5 0 5-5v-8q0-5 5-5m5 0h10m118 0h30m77 0h22m-109 25q0 5 5 5h5m79 0h5q5 0 5-5m-104-25q5 0 5 5v33q0 5 5 5h89q5 0 5-5v-33q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="5" width="53" height="25" rx="7"/><text class="text" x="15" y="22">DROP</text><rect class="literal" x="68" y="5" width="87" height="25" rx="7"/><text class="text" x="78" y="22">SEQUENCE</text><rect class="literal" x="185" y="5" width="32" height="25" rx="7"/><text class="text" x="195" y="22">IF</text><rect class="literal" x="227" y="5" width="64" height="25" rx="7"/><text class="text" x="237" y="22">EXISTS</text><a xlink:href="../grammar_diagrams#sequence-name"><rect class="rule" x="321" y="5" width="118" height="25"/><text class="text" x="331" y="22">sequence_name</text></a><rect class="literal" x="469" y="5" width="77" height="25" rx="7"/><text class="text" x="479" y="22">CASCADE</text><rect class="literal" x="469" y="35" width="79" height="25" rx="7"/><text class="text" x="479" y="52">RESTRICT</text></svg>

### sequence_name
```
sequence_name = '<Text Literal>';
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="117" height="35" viewbox="0 0 117 35"><path class="connector" d="M0 22h5m107 0h5"/><rect class="literal" x="5" y="5" width="107" height="25" rx="7"/><text class="text" x="15" y="22">&lt;Text Literal&gt;</text></svg>

### create_sequence_options
```
create_sequence_options = [ 'INCREMENT' [ 'BY' ] increment ] [ 'MINVALUE' minvalue | 'NO' 'MINVALUE' ] [ 'MAXVALUE' maxvalue | 'NO' 'MAXVALUE' ] [ 'START' [ 'WITH' ] start ] [ 'CACHE' cache ];
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="1205" height="70" viewbox="0 0 1205 70"><path class="connector" d="M0 22h25m92 0h30m35 0h20m-70 0q5 0 5 5v8q0 5 5 5h45q5 0 5-5v-8q0-5 5-5m5 0h10m81 0h20m-303 0q5 0 5 5v23q0 5 5 5h278q5 0 5-5v-23q0-5 5-5m5 0h30m84 0h10m75 0h20m-199 25q0 5 5 5h5m38 0h10m84 0h42q5 0 5-5m-194-25q5 0 5 5v33q0 5 5 5h179q5 0 5-5v-33q0-5 5-5m5 0h30m86 0h10m78 0h20m-204 25q0 5 5 5h5m38 0h10m86 0h45q5 0 5-5m-199-25q5 0 5 5v33q0 5 5 5h184q5 0 5-5v-33q0-5 5-5m5 0h30m58 0h30m53 0h20m-88 0q5 0 5 5v8q0 5 5 5h63q5 0 5-5v-8q0-5 5-5m5 0h10m48 0h20m-254 0q5 0 5 5v23q0 5 5 5h229q5 0 5-5v-23q0-5 5-5m5 0h30m61 0h10m54 0h20m-160 0q5 0 5 5v8q0 5 5 5h135q5 0 5-5v-8q0-5 5-5m5 0h5"/><rect class="literal" x="25" y="5" width="92" height="25" rx="7"/><text class="text" x="35" y="22">INCREMENT</text><rect class="literal" x="147" y="5" width="35" height="25" rx="7"/><text class="text" x="157" y="22">BY</text><a xlink:href="../grammar_diagrams#increment"><rect class="rule" x="212" y="5" width="81" height="25"/><text class="text" x="222" y="22">increment</text></a><rect class="literal" x="343" y="5" width="84" height="25" rx="7"/><text class="text" x="353" y="22">MINVALUE</text><a xlink:href="../grammar_diagrams#minvalue"><rect class="rule" x="437" y="5" width="75" height="25"/><text class="text" x="447" y="22">minvalue</text></a><rect class="literal" x="343" y="35" width="38" height="25" rx="7"/><text class="text" x="353" y="52">NO</text><rect class="literal" x="391" y="35" width="84" height="25" rx="7"/><text class="text" x="401" y="52">MINVALUE</text><rect class="literal" x="562" y="5" width="86" height="25" rx="7"/><text class="text" x="572" y="22">MAXVALUE</text><a xlink:href="../grammar_diagrams#maxvalue"><rect class="rule" x="658" y="5" width="78" height="25"/><text class="text" x="668" y="22">maxvalue</text></a><rect class="literal" x="562" y="35" width="38" height="25" rx="7"/><text class="text" x="572" y="52">NO</text><rect class="literal" x="610" y="35" width="86" height="25" rx="7"/><text class="text" x="620" y="52">MAXVALUE</text><rect class="literal" x="786" y="5" width="58" height="25" rx="7"/><text class="text" x="796" y="22">START</text><rect class="literal" x="874" y="5" width="53" height="25" rx="7"/><text class="text" x="884" y="22">WITH</text><a xlink:href="../grammar_diagrams#start"><rect class="rule" x="957" y="5" width="48" height="25"/><text class="text" x="967" y="22">start</text></a><rect class="literal" x="1055" y="5" width="61" height="25" rx="7"/><text class="text" x="1065" y="22">CACHE</text><a xlink:href="../grammar_diagrams#cache"><rect class="rule" x="1126" y="5" width="54" height="25"/><text class="text" x="1136" y="22">cache</text></a></svg>

### increment
```
increment = '<Integer Literal>';
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="135" height="35" viewbox="0 0 135 35"><path class="connector" d="M0 22h5m125 0h5"/><rect class="literal" x="5" y="5" width="125" height="25" rx="7"/><text class="text" x="15" y="22">&lt;Integer Literal&gt;</text></svg>

### minvalue
```
minvalue = '<Integer Literal>';
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="135" height="35" viewbox="0 0 135 35"><path class="connector" d="M0 22h5m125 0h5"/><rect class="literal" x="5" y="5" width="125" height="25" rx="7"/><text class="text" x="15" y="22">&lt;Integer Literal&gt;</text></svg>

### maxvalue
```
maxvalue = '<Integer Literal>';
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="135" height="35" viewbox="0 0 135 35"><path class="connector" d="M0 22h5m125 0h5"/><rect class="literal" x="5" y="5" width="125" height="25" rx="7"/><text class="text" x="15" y="22">&lt;Integer Literal&gt;</text></svg>

### start
```
start = '<Integer Literal>';
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="135" height="35" viewbox="0 0 135 35"><path class="connector" d="M0 22h5m125 0h5"/><rect class="literal" x="5" y="5" width="125" height="25" rx="7"/><text class="text" x="15" y="22">&lt;Integer Literal&gt;</text></svg>

### cache
```
cache = '<Integer Literal>';
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="135" height="35" viewbox="0 0 135 35"><path class="connector" d="M0 22h5m125 0h5"/><rect class="literal" x="5" y="5" width="125" height="25" rx="7"/><text class="text" x="15" y="22">&lt;Integer Literal&gt;</text></svg>

### nextval
```
nextval = 'SELECT' 'nextval(' sequence_name ')';
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="318" height="35" viewbox="0 0 318 35"><path class="connector" d="M0 22h5m66 0h10m69 0h10m118 0h10m25 0h5"/><rect class="literal" x="5" y="5" width="66" height="25" rx="7"/><text class="text" x="15" y="22">SELECT</text><rect class="literal" x="81" y="5" width="69" height="25" rx="7"/><text class="text" x="91" y="22">nextval(</text><a xlink:href="../grammar_diagrams#sequence-name"><rect class="rule" x="160" y="5" width="118" height="25"/><text class="text" x="170" y="22">sequence_name</text></a><rect class="literal" x="288" y="5" width="25" height="25" rx="7"/><text class="text" x="298" y="22">)</text></svg>

### currval
```
currval = 'SELECT' 'currval(' sequence_name ')';
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="315" height="35" viewbox="0 0 315 35"><path class="connector" d="M0 22h5m66 0h10m66 0h10m118 0h10m25 0h5"/><rect class="literal" x="5" y="5" width="66" height="25" rx="7"/><text class="text" x="15" y="22">SELECT</text><rect class="literal" x="81" y="5" width="66" height="25" rx="7"/><text class="text" x="91" y="22">currval(</text><a xlink:href="../grammar_diagrams#sequence-name"><rect class="rule" x="157" y="5" width="118" height="25"/><text class="text" x="167" y="22">sequence_name</text></a><rect class="literal" x="285" y="5" width="25" height="25" rx="7"/><text class="text" x="295" y="22">)</text></svg>

### lastval
```
lastval = 'SELECT' 'lastval()';
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="154" height="35" viewbox="0 0 154 35"><path class="connector" d="M0 22h5m66 0h10m68 0h5"/><rect class="literal" x="5" y="5" width="66" height="25" rx="7"/><text class="text" x="15" y="22">SELECT</text><rect class="literal" x="81" y="5" width="68" height="25" rx="7"/><text class="text" x="91" y="22">lastval()</text></svg>
