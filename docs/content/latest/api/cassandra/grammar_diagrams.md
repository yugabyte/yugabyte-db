---
title: Grammar Diagrams
summary: Diagrams of the grammar rules
aliases:
  - api/cassandra/grammar_diagrams
  - api/cql/grammar_diagrams
  - api/ycql/grammar_diagrams
---

## DDL Statements

### alter_table
```
alter_table = 'ALTER' 'TABLE' table_name ( 'ADD' ( column_name column_type ) { ',' ( column_name column_type ) } | 'DROP' ( column_name { ',' column_name } ) | 'RENAME' ( column_name 'TO' column_name ) { ',' ( column_name 'TO' column_name ) } | 'WITH' ( property_name '=' property_literal ) { 'AND' ( property_name '=' property_literal ) } { 'ADD' ( column_name column_type ) { ',' ( column_name column_type ) } | 'DROP' ( column_name { ',' column_name } ) | 'RENAME' ( column_name 'TO' column_name ) { ',' ( column_name 'TO' column_name ) } | 'WITH' ( property_name '=' property_literal ) { 'AND' ( property_name '=' property_literal ) } } );
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="716" height="260" viewbox="0 0 716 260"><path class="connector" d="M0 67h5m58 0h10m58 0h10m91 0h30m-5 0q-5 0-5-5v-47q0-5 5-5h439q5 0 5 5v47q0 5-5 5m-434 0h20m46 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h100m24 0h100q5 0 5 5v20q0 5-5 5m-113 0h10m98 0h119m-419 55q0 5 5 5h5m53 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h46m24 0h46q5 0 5 5v20q0 5-5 5m-5 0h205q5 0 5-5m-409 60q0 5 5 5h5m71 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h127m24 0h127q5 0 5 5v20q0 5-5 5m-167 0h10m36 0h10m106 0h25q5 0 5-5m-414-115q5 0 5 5v170q0 5 5 5h5m53 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h118m46 0h119q5 0 5 5v20q0 5-5 5m-166 0h10m30 0h10m111 0h38q5 0 5-5v-170q0-5 5-5m5 0h25"/><rect class="literal" x="5" y="50" width="58" height="25" rx="7"/><text class="text" x="15" y="67">ALTER</text><rect class="literal" x="73" y="50" width="58" height="25" rx="7"/><text class="text" x="83" y="67">TABLE</text><a xlink:href="../grammar_diagrams#table-name"><rect class="rule" x="141" y="50" width="91" height="25"/><text class="text" x="151" y="67">table_name</text></a><rect class="literal" x="282" y="50" width="46" height="25" rx="7"/><text class="text" x="292" y="67">ADD</text><rect class="literal" x="453" y="20" width="24" height="25" rx="7"/><text class="text" x="463" y="37">,</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="358" y="50" width="106" height="25"/><text class="text" x="368" y="67">column_name</text></a><a xlink:href="../grammar_diagrams#column-type"><rect class="rule" x="474" y="50" width="98" height="25"/><text class="text" x="484" y="67">column_type</text></a><rect class="literal" x="282" y="110" width="53" height="25" rx="7"/><text class="text" x="292" y="127">DROP</text><rect class="literal" x="406" y="80" width="24" height="25" rx="7"/><text class="text" x="416" y="97">,</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="365" y="110" width="106" height="25"/><text class="text" x="375" y="127">column_name</text></a><rect class="literal" x="282" y="170" width="71" height="25" rx="7"/><text class="text" x="292" y="187">RENAME</text><rect class="literal" x="505" y="140" width="24" height="25" rx="7"/><text class="text" x="515" y="157">,</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="383" y="170" width="106" height="25"/><text class="text" x="393" y="187">column_name</text></a><rect class="literal" x="499" y="170" width="36" height="25" rx="7"/><text class="text" x="509" y="187">TO</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="545" y="170" width="106" height="25"/><text class="text" x="555" y="187">column_name</text></a><rect class="literal" x="282" y="230" width="53" height="25" rx="7"/><text class="text" x="292" y="247">WITH</text><rect class="literal" x="478" y="200" width="46" height="25" rx="7"/><text class="text" x="488" y="217">AND</text><a xlink:href="../grammar_diagrams#property-name"><rect class="rule" x="365" y="230" width="112" height="25"/><text class="text" x="375" y="247">property_name</text></a><rect class="literal" x="487" y="230" width="30" height="25" rx="7"/><text class="text" x="497" y="247">=</text><a xlink:href="../grammar_diagrams#property-literal"><rect class="rule" x="527" y="230" width="111" height="25"/><text class="text" x="537" y="247">property_literal</text></a></svg>

### column_type
```
column_type = '<type>';
```
- See [Data Types](../#data-types).

### create_index
```
create_index = 'CREATE' [ 'UNIQUE' ] 'INDEX' [ 'IF' 'NOT' 'EXISTS' ] index_name 'ON' table_name '(' index_columns ')'
               [ included_columns ] [ clustering_key_column_ordering ];
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="919" height="100" viewbox="0 0 919 100"><path class="connector" d="M0 22h5m67 0h30m69 0h20m-104 0q5 0 5 5v8q0 5 5 5h79q5 0 5-5v-8q0-5 5-5m5 0h10m59 0h30m32 0h10m45 0h10m64 0h20m-196 0q5 0 5 5v8q0 5 5 5h171q5 0 5-5v-8q0-5 5-5m5 0h10m94 0h10m38 0h10m91 0h10m25 0h10m110 0h10m25 0h5m-919 50h25m127 0h20m-162 0q5 0 5 5v8q0 5 5 5h137q5 0 5-5v-8q0-5 5-5m5 0h30m216 0h20m-251 0q5 0 5 5v8q0 5 5 5h226q5 0 5-5v-8q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="5" width="67" height="25" rx="7"/><text class="text" x="15" y="22">CREATE</text><rect class="literal" x="102" y="5" width="69" height="25" rx="7"/><text class="text" x="112" y="22">UNIQUE</text><rect class="literal" x="201" y="5" width="59" height="25" rx="7"/><text class="text" x="211" y="22">INDEX</text><rect class="literal" x="290" y="5" width="32" height="25" rx="7"/><text class="text" x="300" y="22">IF</text><rect class="literal" x="332" y="5" width="45" height="25" rx="7"/><text class="text" x="342" y="22">NOT</text><rect class="literal" x="387" y="5" width="64" height="25" rx="7"/><text class="text" x="397" y="22">EXISTS</text><a xlink:href="../grammar_diagrams#index-name"><rect class="rule" x="481" y="5" width="94" height="25"/><text class="text" x="491" y="22">index_name</text></a><rect class="literal" x="585" y="5" width="38" height="25" rx="7"/><text class="text" x="595" y="22">ON</text><a xlink:href="../grammar_diagrams#table-name"><rect class="rule" x="633" y="5" width="91" height="25"/><text class="text" x="643" y="22">table_name</text></a><rect class="literal" x="734" y="5" width="25" height="25" rx="7"/><text class="text" x="744" y="22">(</text><a xlink:href="../grammar_diagrams#index-columns"><rect class="rule" x="769" y="5" width="110" height="25"/><text class="text" x="779" y="22">index_columns</text></a><rect class="literal" x="889" y="5" width="25" height="25" rx="7"/><text class="text" x="899" y="22">)</text><a xlink:href="../grammar_diagrams#included-columns"><rect class="rule" x="25" y="55" width="127" height="25"/><text class="text" x="35" y="72">included_columns</text></a><a xlink:href="../grammar_diagrams#clustering-key-column-ordering"><rect class="rule" x="202" y="55" width="216" height="25"/><text class="text" x="212" y="72">clustering_key_column_ordering</text></a></svg>

### index_columns
```
index_columns = partition_key_columns [ clustering_key_columns ];
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="382" height="50" viewbox="0 0 382 50"><path class="connector" d="M0 22h5m157 0h30m165 0h20m-200 0q5 0 5 5v8q0 5 5 5h175q5 0 5-5v-8q0-5 5-5m5 0h5"/><a xlink:href="../grammar_diagrams#partition-key-columns"><rect class="rule" x="5" y="5" width="157" height="25"/><text class="text" x="15" y="22">partition_key_columns</text></a><a xlink:href="../grammar_diagrams#clustering-key-columns"><rect class="rule" x="192" y="5" width="165" height="25"/><text class="text" x="202" y="22">clustering_key_columns</text></a></svg>

### partition_key_columns
```
partition_key_columns = column_name | '(' column_name { ',' column_name } ')';
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="266" height="95" viewbox="0 0 266 95"><path class="connector" d="M0 22h25m106 0h130m-251 0q5 0 5 5v50q0 5 5 5h5m25 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h46m24 0h46q5 0 5 5v20q0 5-5 5m-5 0h30m25 0h5q5 0 5-5v-50q0-5 5-5m5 0h5"/><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="25" y="5" width="106" height="25"/><text class="text" x="35" y="22">column_name</text></a><rect class="literal" x="25" y="65" width="25" height="25" rx="7"/><text class="text" x="35" y="82">(</text><rect class="literal" x="121" y="35" width="24" height="25" rx="7"/><text class="text" x="131" y="52">,</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="80" y="65" width="106" height="25"/><text class="text" x="90" y="82">column_name</text></a><rect class="literal" x="216" y="65" width="25" height="25" rx="7"/><text class="text" x="226" y="82">)</text></svg>

### clustering_key_columns
```
clustering_key_columns = column_name { ',' column_name };
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="156" height="65" viewbox="0 0 156 65"><path class="connector" d="M0 52h25m-5 0q-5 0-5-5v-20q0-5 5-5h46m24 0h46q5 0 5 5v20q0 5-5 5m-5 0h25"/><rect class="literal" x="66" y="5" width="24" height="25" rx="7"/><text class="text" x="76" y="22">,</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="25" y="35" width="106" height="25"/><text class="text" x="35" y="52">column_name</text></a></svg>

### included_columns
```
included_columns = 'INCLUDE' '(' column_name { ',' column_name } ')';
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="311" height="65" viewbox="0 0 311 65"><path class="connector" d="M0 52h5m75 0h10m25 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h46m24 0h46q5 0 5 5v20q0 5-5 5m-5 0h30m25 0h5"/><rect class="literal" x="5" y="35" width="75" height="25" rx="7"/><text class="text" x="15" y="52">INCLUDE</text><rect class="literal" x="90" y="35" width="25" height="25" rx="7"/><text class="text" x="100" y="52">(</text><rect class="literal" x="186" y="5" width="24" height="25" rx="7"/><text class="text" x="196" y="22">,</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="145" y="35" width="106" height="25"/><text class="text" x="155" y="52">column_name</text></a><rect class="literal" x="281" y="35" width="25" height="25" rx="7"/><text class="text" x="291" y="52">)</text></svg>

### clustering_key_column_ordering
```
clustering_key_column_ordering = 'WITH' 'CLUSTERING' 'ORDER' 'BY' '(' ( column_name [ 'ASC' | 'DESC' ] ) { ',' ( column_name [ 'ASC' | 'DESC' ] ) } ')';
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="617" height="100" viewbox="0 0 617 100"><path class="connector" d="M0 52h5m53 0h10m98 0h10m62 0h10m35 0h10m25 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h97m24 0h98q5 0 5 5v20q0 5-5 5m-108 0h30m44 0h29m-83 25q0 5 5 5h5m53 0h5q5 0 5-5m-78-25q5 0 5 5v33q0 5 5 5h63q5 0 5-5v-33q0-5 5-5m5 0h30m25 0h5"/><rect class="literal" x="5" y="35" width="53" height="25" rx="7"/><text class="text" x="15" y="52">WITH</text><rect class="literal" x="68" y="35" width="98" height="25" rx="7"/><text class="text" x="78" y="52">CLUSTERING</text><rect class="literal" x="176" y="35" width="62" height="25" rx="7"/><text class="text" x="186" y="52">ORDER</text><rect class="literal" x="248" y="35" width="35" height="25" rx="7"/><text class="text" x="258" y="52">BY</text><rect class="literal" x="293" y="35" width="25" height="25" rx="7"/><text class="text" x="303" y="52">(</text><rect class="literal" x="440" y="5" width="24" height="25" rx="7"/><text class="text" x="450" y="22">,</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="348" y="35" width="106" height="25"/><text class="text" x="358" y="52">column_name</text></a><rect class="literal" x="484" y="35" width="44" height="25" rx="7"/><text class="text" x="494" y="52">ASC</text><rect class="literal" x="484" y="65" width="53" height="25" rx="7"/><text class="text" x="494" y="82">DESC</text><rect class="literal" x="587" y="35" width="25" height="25" rx="7"/><text class="text" x="597" y="52">)</text></svg>

### create_keyspace
```
create_keyspace = 'CREATE' ( 'KEYSPACE' | 'SCHEMA' ) [ 'IF' 'NOT' 'EXISTS' ] keyspace_name keyspace_properties;
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="697" height="65" viewbox="0 0 697 65"><path class="connector" d="M0 22h5m67 0h30m82 0h20m-117 0q5 0 5 5v20q0 5 5 5h5m71 0h16q5 0 5-5v-20q0-5 5-5m5 0h30m32 0h10m45 0h10m64 0h20m-196 0q5 0 5 5v8q0 5 5 5h171q5 0 5-5v-8q0-5 5-5m5 0h10m116 0h10m141 0h5"/><rect class="literal" x="5" y="5" width="67" height="25" rx="7"/><text class="text" x="15" y="22">CREATE</text><rect class="literal" x="102" y="5" width="82" height="25" rx="7"/><text class="text" x="112" y="22">KEYSPACE</text><rect class="literal" x="102" y="35" width="71" height="25" rx="7"/><text class="text" x="112" y="52">SCHEMA</text><rect class="literal" x="234" y="5" width="32" height="25" rx="7"/><text class="text" x="244" y="22">IF</text><rect class="literal" x="276" y="5" width="45" height="25" rx="7"/><text class="text" x="286" y="22">NOT</text><rect class="literal" x="331" y="5" width="64" height="25" rx="7"/><text class="text" x="341" y="22">EXISTS</text><a xlink:href="../grammar_diagrams#keyspace-name"><rect class="rule" x="425" y="5" width="116" height="25"/><text class="text" x="435" y="22">keyspace_name</text></a><a xlink:href="../grammar_diagrams#keyspace-properties"><rect class="rule" x="551" y="5" width="141" height="25"/><text class="text" x="561" y="22">keyspace_properties</text></a></svg>

### keyspace_properties
```
keyspace_properties = [ 'WITH' 'REPLICATION' '=' '{' keyspace_property { ',' keyspace_property } '}' ] [ 'AND' 'DURABLE_WRITES' '=' ( 'true' | 'false' ) ];
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="888" height="110" viewbox="0 0 888 110"><path class="connector" d="M0 52h25m53 0h10m101 0h10m30 0h10m28 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h59m24 0h59q5 0 5 5v20q0 5-5 5m-5 0h30m28 0h20m-497 0q5 0 5 5v8q0 5 5 5h472q5 0 5-5v-8q0-5 5-5m5 0h30m46 0h10m133 0h10m30 0h30m45 0h22m-82 0q5 0 5 5v20q0 5 5 5h5m47 0h5q5 0 5-5v-20q0-5 5-5m5 0h20m-361 0q5 0 5 5v38q0 5 5 5h336q5 0 5-5v-38q0-5 5-5m5 0h5"/><rect class="literal" x="25" y="35" width="53" height="25" rx="7"/><text class="text" x="35" y="52">WITH</text><rect class="literal" x="88" y="35" width="101" height="25" rx="7"/><text class="text" x="98" y="52">REPLICATION</text><rect class="literal" x="199" y="35" width="30" height="25" rx="7"/><text class="text" x="209" y="52">=</text><rect class="literal" x="239" y="35" width="28" height="25" rx="7"/><text class="text" x="249" y="52">{</text><rect class="literal" x="351" y="5" width="24" height="25" rx="7"/><text class="text" x="361" y="22">,</text><a xlink:href="../grammar_diagrams#keyspace-property"><rect class="rule" x="297" y="35" width="132" height="25"/><text class="text" x="307" y="52">keyspace_property</text></a><rect class="literal" x="459" y="35" width="28" height="25" rx="7"/><text class="text" x="469" y="52">}</text><rect class="literal" x="537" y="35" width="46" height="25" rx="7"/><text class="text" x="547" y="52">AND</text><rect class="literal" x="593" y="35" width="133" height="25" rx="7"/><text class="text" x="603" y="52">DURABLE_WRITES</text><rect class="literal" x="736" y="35" width="30" height="25" rx="7"/><text class="text" x="746" y="52">=</text><rect class="literal" x="796" y="35" width="45" height="25" rx="7"/><text class="text" x="806" y="52">true</text><rect class="literal" x="796" y="65" width="47" height="25" rx="7"/><text class="text" x="806" y="82">false</text></svg>

### keyspace_property
```
keyspace_property = property_name '=' property_literal;
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="283" height="35" viewbox="0 0 283 35"><path class="connector" d="M0 22h5m112 0h10m30 0h10m111 0h5"/><a xlink:href="../grammar_diagrams#property-name"><rect class="rule" x="5" y="5" width="112" height="25"/><text class="text" x="15" y="22">property_name</text></a><rect class="literal" x="127" y="5" width="30" height="25" rx="7"/><text class="text" x="137" y="22">=</text><a xlink:href="../grammar_diagrams#property-literal"><rect class="rule" x="167" y="5" width="111" height="25"/><text class="text" x="177" y="22">property_literal</text></a></svg>

### create_table
```
create_table = 'CREATE' 'TABLE' [ 'IF' 'NOT' 'EXISTS' ] table_name '(' table_schema ')' [ table_properties ];
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="806" height="50" viewbox="0 0 806 50"><path class="connector" d="M0 22h5m67 0h10m58 0h30m32 0h10m45 0h10m64 0h20m-196 0q5 0 5 5v8q0 5 5 5h171q5 0 5-5v-8q0-5 5-5m5 0h10m91 0h10m25 0h10m103 0h10m25 0h30m116 0h20m-151 0q5 0 5 5v8q0 5 5 5h126q5 0 5-5v-8q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="5" width="67" height="25" rx="7"/><text class="text" x="15" y="22">CREATE</text><rect class="literal" x="82" y="5" width="58" height="25" rx="7"/><text class="text" x="92" y="22">TABLE</text><rect class="literal" x="170" y="5" width="32" height="25" rx="7"/><text class="text" x="180" y="22">IF</text><rect class="literal" x="212" y="5" width="45" height="25" rx="7"/><text class="text" x="222" y="22">NOT</text><rect class="literal" x="267" y="5" width="64" height="25" rx="7"/><text class="text" x="277" y="22">EXISTS</text><a xlink:href="../grammar_diagrams#table-name"><rect class="rule" x="361" y="5" width="91" height="25"/><text class="text" x="371" y="22">table_name</text></a><rect class="literal" x="462" y="5" width="25" height="25" rx="7"/><text class="text" x="472" y="22">(</text><a xlink:href="../grammar_diagrams#table-schema"><rect class="rule" x="497" y="5" width="103" height="25"/><text class="text" x="507" y="22">table_schema</text></a><rect class="literal" x="610" y="5" width="25" height="25" rx="7"/><text class="text" x="620" y="22">)</text><a xlink:href="../grammar_diagrams#table-properties"><rect class="rule" x="665" y="5" width="116" height="25"/><text class="text" x="675" y="22">table_properties</text></a></svg>

### table_schema
```
table_schema = ( column_name column_type ( 'PRIMARY' 'KEY' | 'STATIC' { 'PRIMARY' 'KEY' | 'STATIC' } ) | 'PRIMARY' 'KEY' '(' '(' column_name { ',' column_name } ')' { ',' column_name } ')' ) { ',' ( column_name column_type ( 'PRIMARY' 'KEY' | 'STATIC' { 'PRIMARY' 'KEY' | 'STATIC' } ) | 'PRIMARY' 'KEY' '(' '(' column_name { ',' column_name } ')' { ',' column_name } ')' ) };
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="742" height="185" viewbox="0 0 742 185"><path class="connector" d="M0 67h25m-5 0q-5 0-5-5v-35q0-5 5-5h339m24 0h339q5 0 5 5v35q0 5-5 5m-697 0h20m106 0h10m98 0h30m-5 0q-5 0-5-5v-17q0-5 5-5h176q5 0 5 5v17q0 5-5 5m-171 0h20m73 0h10m43 0h20m-161 0q5 0 5 5v20q0 5 5 5h5m63 0h68q5 0 5-5v-20q0-5 5-5m5 0h262m-687 0q5 0 5 5v80q0 5 5 5h5m73 0h10m43 0h10m25 0h10m25 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h46m24 0h46q5 0 5 5v20q0 5-5 5m-5 0h30m25 0h50m-5 0q-5 0-5-5v-17q0-5 5-5h150q5 0 5 5v17q0 5-5 5m-121 0h10m106 0h40m-215 0q5 0 5 5v8q0 5 5 5h190q5 0 5-5v-8q0-5 5-5m5 0h10m25 0h5q5 0 5-5v-80q0-5 5-5m5 0h25"/><rect class="literal" x="359" y="5" width="24" height="25" rx="7"/><text class="text" x="369" y="22">,</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="45" y="50" width="106" height="25"/><text class="text" x="55" y="67">column_name</text></a><a xlink:href="../grammar_diagrams#column-type"><rect class="rule" x="161" y="50" width="98" height="25"/><text class="text" x="171" y="67">column_type</text></a><rect class="literal" x="309" y="50" width="73" height="25" rx="7"/><text class="text" x="319" y="67">PRIMARY</text><rect class="literal" x="392" y="50" width="43" height="25" rx="7"/><text class="text" x="402" y="67">KEY</text><rect class="literal" x="309" y="80" width="63" height="25" rx="7"/><text class="text" x="319" y="97">STATIC</text><rect class="literal" x="45" y="140" width="73" height="25" rx="7"/><text class="text" x="55" y="157">PRIMARY</text><rect class="literal" x="128" y="140" width="43" height="25" rx="7"/><text class="text" x="138" y="157">KEY</text><rect class="literal" x="181" y="140" width="25" height="25" rx="7"/><text class="text" x="191" y="157">(</text><rect class="literal" x="216" y="140" width="25" height="25" rx="7"/><text class="text" x="226" y="157">(</text><rect class="literal" x="312" y="110" width="24" height="25" rx="7"/><text class="text" x="322" y="127">,</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="271" y="140" width="106" height="25"/><text class="text" x="281" y="157">column_name</text></a><rect class="literal" x="407" y="140" width="25" height="25" rx="7"/><text class="text" x="417" y="157">)</text><rect class="literal" x="482" y="140" width="24" height="25" rx="7"/><text class="text" x="492" y="157">,</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="516" y="140" width="106" height="25"/><text class="text" x="526" y="157">column_name</text></a><rect class="literal" x="672" y="140" width="25" height="25" rx="7"/><text class="text" x="682" y="157">)</text></svg>

### table_properties
```
table_properties = 'WITH' ( property_name = property_literal | 'CLUSTERING' 'ORDER' 'BY' '(' ( column_name [ 'ASC' | 'DESC' ] ) { ',' ( column_name [ 'ASC' | 'DESC' ] ) } ')' | 'COMPACT' 'STORAGE' ) { 'AND' ( property_name = property_literal | 'CLUSTERING' 'ORDER' 'BY' '(' ( column_name [ 'ASC' | 'DESC' ] ) { ',' ( column_name [ 'ASC' | 'DESC' ] ) } ')' | 'COMPACT' 'STORAGE' ) };
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="697" height="190" viewbox="0 0 697 190"><path class="connector" d="M0 52h5m53 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h274m46 0h274q5 0 5 5v20q0 5-5 5m-589 0h20m112 0h10m30 0h10m111 0h291m-574 55q0 5 5 5h5m98 0h10m62 0h10m35 0h10m25 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h97m24 0h98q5 0 5 5v20q0 5-5 5m-108 0h30m44 0h29m-83 25q0 5 5 5h5m53 0h5q5 0 5-5m-78-25q5 0 5 5v33q0 5 5 5h63q5 0 5-5v-33q0-5 5-5m5 0h30m25 0h5q5 0 5-5m-569-55q5 0 5 5v115q0 5 5 5h5m77 0h10m77 0h385q5 0 5-5v-115q0-5 5-5m5 0h25"/><rect class="literal" x="5" y="35" width="53" height="25" rx="7"/><text class="text" x="15" y="52">WITH</text><rect class="literal" x="357" y="5" width="46" height="25" rx="7"/><text class="text" x="367" y="22">AND</text><a xlink:href="../grammar_diagrams#property-name"><rect class="rule" x="108" y="35" width="112" height="25"/><text class="text" x="118" y="52">property_name</text></a><a xlink:href="../grammar_diagrams#="><rect class="rule" x="230" y="35" width="30" height="25"/><text class="text" x="240" y="52">=</text></a><a xlink:href="../grammar_diagrams#property-literal"><rect class="rule" x="270" y="35" width="111" height="25"/><text class="text" x="280" y="52">property_literal</text></a><rect class="literal" x="108" y="95" width="98" height="25" rx="7"/><text class="text" x="118" y="112">CLUSTERING</text><rect class="literal" x="216" y="95" width="62" height="25" rx="7"/><text class="text" x="226" y="112">ORDER</text><rect class="literal" x="288" y="95" width="35" height="25" rx="7"/><text class="text" x="298" y="112">BY</text><rect class="literal" x="333" y="95" width="25" height="25" rx="7"/><text class="text" x="343" y="112">(</text><rect class="literal" x="480" y="65" width="24" height="25" rx="7"/><text class="text" x="490" y="82">,</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="388" y="95" width="106" height="25"/><text class="text" x="398" y="112">column_name</text></a><rect class="literal" x="524" y="95" width="44" height="25" rx="7"/><text class="text" x="534" y="112">ASC</text><rect class="literal" x="524" y="125" width="53" height="25" rx="7"/><text class="text" x="534" y="142">DESC</text><rect class="literal" x="627" y="95" width="25" height="25" rx="7"/><text class="text" x="637" y="112">)</text><rect class="literal" x="108" y="160" width="77" height="25" rx="7"/><text class="text" x="118" y="177">COMPACT</text><rect class="literal" x="195" y="160" width="77" height="25" rx="7"/><text class="text" x="205" y="177">STORAGE</text></svg>

### create_type
```
create_type = 'CREATE' 'TYPE' [ 'IF' 'NOT' 'EXISTS' ] type_name '(' ( field_name field_type ) { ',' ( field_name field_type ) } ')';
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="739" height="80" viewbox="0 0 739 80"><path class="connector" d="M0 52h5m67 0h10m49 0h30m32 0h10m45 0h10m64 0h20m-196 0q5 0 5 5v8q0 5 5 5h171q5 0 5-5v-8q0-5 5-5m5 0h10m88 0h10m25 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h80m24 0h80q5 0 5 5v20q0 5-5 5m-93 0h10m78 0h30m25 0h5"/><rect class="literal" x="5" y="35" width="67" height="25" rx="7"/><text class="text" x="15" y="52">CREATE</text><rect class="literal" x="82" y="35" width="49" height="25" rx="7"/><text class="text" x="92" y="52">TYPE</text><rect class="literal" x="161" y="35" width="32" height="25" rx="7"/><text class="text" x="171" y="52">IF</text><rect class="literal" x="203" y="35" width="45" height="25" rx="7"/><text class="text" x="213" y="52">NOT</text><rect class="literal" x="258" y="35" width="64" height="25" rx="7"/><text class="text" x="268" y="52">EXISTS</text><a xlink:href="../grammar_diagrams#type-name"><rect class="rule" x="352" y="35" width="88" height="25"/><text class="text" x="362" y="52">type_name</text></a><rect class="literal" x="450" y="35" width="25" height="25" rx="7"/><text class="text" x="460" y="52">(</text><rect class="literal" x="580" y="5" width="24" height="25" rx="7"/><text class="text" x="590" y="22">,</text><a xlink:href="../grammar_diagrams#field-name"><rect class="rule" x="505" y="35" width="86" height="25"/><text class="text" x="515" y="52">field_name</text></a><a xlink:href="../grammar_diagrams#field-type"><rect class="rule" x="601" y="35" width="78" height="25"/><text class="text" x="611" y="52">field_type</text></a><rect class="literal" x="709" y="35" width="25" height="25" rx="7"/><text class="text" x="719" y="52">)</text></svg>

### field_type
```
field_type = '<type>';
```
- See [Data Types](../#data-types).

### drop_index
```
drop_index = 'DROP' 'INDEX' [ 'IF' 'EXISTS' ] index_name;
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="388" height="50" viewbox="0 0 388 50"><path class="connector" d="M0 22h5m53 0h10m58 0h30m32 0h10m64 0h20m-141 0q5 0 5 5v8q0 5 5 5h116q5 0 5-5v-8q0-5 5-5m5 0h10m91 0h5"/><rect class="literal" x="5" y="5" width="53" height="25" rx="7"/><text class="text" x="15" y="22">DROP</text><rect class="literal" x="68" y="5" width="58" height="25" rx="7"/><text class="text" x="78" y="22">INDEX</text><rect class="literal" x="156" y="5" width="32" height="25" rx="7"/><text class="text" x="166" y="22">IF</text><rect class="literal" x="198" y="5" width="64" height="25" rx="7"/><text class="text" x="208" y="22">EXISTS</text><a xlink:href="../grammar_diagrams#index-name"><rect class="rule" x="292" y="5" width="91" height="25"/><text class="text" x="302" y="22">index_name</text></a></svg>

### drop_keyspace
```
drop_keyspace = 'DROP' ( 'KEYSPACE' | 'SCHEMA' ) [ 'IF' 'EXISTS' ] keyspace_name;
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="477" height="65" viewbox="0 0 477 65"><path class="connector" d="M0 22h5m53 0h30m82 0h20m-117 0q5 0 5 5v20q0 5 5 5h5m71 0h16q5 0 5-5v-20q0-5 5-5m5 0h30m32 0h10m64 0h20m-141 0q5 0 5 5v8q0 5 5 5h116q5 0 5-5v-8q0-5 5-5m5 0h10m116 0h5"/><rect class="literal" x="5" y="5" width="53" height="25" rx="7"/><text class="text" x="15" y="22">DROP</text><rect class="literal" x="88" y="5" width="82" height="25" rx="7"/><text class="text" x="98" y="22">KEYSPACE</text><rect class="literal" x="88" y="35" width="71" height="25" rx="7"/><text class="text" x="98" y="52">SCHEMA</text><rect class="literal" x="220" y="5" width="32" height="25" rx="7"/><text class="text" x="230" y="22">IF</text><rect class="literal" x="262" y="5" width="64" height="25" rx="7"/><text class="text" x="272" y="22">EXISTS</text><a xlink:href="../grammar_diagrams#keyspace-name"><rect class="rule" x="356" y="5" width="116" height="25"/><text class="text" x="366" y="22">keyspace_name</text></a></svg>

### drop_table
```
drop_table = 'DROP' 'TABLE' [ 'IF' 'EXISTS' ] table_name;
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="388" height="50" viewbox="0 0 388 50"><path class="connector" d="M0 22h5m53 0h10m58 0h30m32 0h10m64 0h20m-141 0q5 0 5 5v8q0 5 5 5h116q5 0 5-5v-8q0-5 5-5m5 0h10m91 0h5"/><rect class="literal" x="5" y="5" width="53" height="25" rx="7"/><text class="text" x="15" y="22">DROP</text><rect class="literal" x="68" y="5" width="58" height="25" rx="7"/><text class="text" x="78" y="22">TABLE</text><rect class="literal" x="156" y="5" width="32" height="25" rx="7"/><text class="text" x="166" y="22">IF</text><rect class="literal" x="198" y="5" width="64" height="25" rx="7"/><text class="text" x="208" y="22">EXISTS</text><a xlink:href="../grammar_diagrams#table-name"><rect class="rule" x="292" y="5" width="91" height="25"/><text class="text" x="302" y="22">table_name</text></a></svg>

### drop_type
```
drop_type = 'DROP' 'TYPE' [ 'IF' 'EXISTS' ] type_name;
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="376" height="50" viewbox="0 0 376 50"><path class="connector" d="M0 22h5m53 0h10m49 0h30m32 0h10m64 0h20m-141 0q5 0 5 5v8q0 5 5 5h116q5 0 5-5v-8q0-5 5-5m5 0h10m88 0h5"/><rect class="literal" x="5" y="5" width="53" height="25" rx="7"/><text class="text" x="15" y="22">DROP</text><rect class="literal" x="68" y="5" width="49" height="25" rx="7"/><text class="text" x="78" y="22">TYPE</text><rect class="literal" x="147" y="5" width="32" height="25" rx="7"/><text class="text" x="157" y="22">IF</text><rect class="literal" x="189" y="5" width="64" height="25" rx="7"/><text class="text" x="199" y="22">EXISTS</text><a xlink:href="../grammar_diagrams#type-name"><rect class="rule" x="283" y="5" width="88" height="25"/><text class="text" x="293" y="22">type_name</text></a></svg>

### use_keyspace
```
use_keyspace = 'USE' keyspace_name;
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="181" height="35" viewbox="0 0 181 35"><path class="connector" d="M0 22h5m45 0h10m116 0h5"/><rect class="literal" x="5" y="5" width="45" height="25" rx="7"/><text class="text" x="15" y="22">USE</text><a xlink:href="../grammar_diagrams#keyspace-name"><rect class="rule" x="60" y="5" width="116" height="25"/><text class="text" x="70" y="22">keyspace_name</text></a></svg>

## DML Statements

### delete
```
delete = 'DELETE' 'FROM' [ 'USING' 'TIMESTAMP' timestamp_expression ] table_name 'WHERE' where_expression [ 'IF' ( [ 'NOT' ] 'EXISTS' | if_expression ) ];
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="1121" height="95" viewbox="0 0 1121 95"><path class="connector" d="M0 22h5m67 0h10m54 0h10m91 0h30m60 0h10m90 0h10m155 0h20m-360 0q5 0 5 5v8q0 5 5 5h335q5 0 5-5v-8q0-5 5-5m5 0h10m65 0h10m128 0h30m32 0h50m45 0h20m-80 0q5 0 5 5v8q0 5 5 5h55q5 0 5-5v-8q0-5 5-5m5 0h10m64 0h20m-194 0q5 0 5 5v35q0 5 5 5h5m98 0h66q5 0 5-5v-35q0-5 5-5m5 0h20m-276 0q5 0 5 5v53q0 5 5 5h251q5 0 5-5v-53q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="5" width="67" height="25" rx="7"/><text class="text" x="15" y="22">DELETE</text><rect class="literal" x="82" y="5" width="54" height="25" rx="7"/><text class="text" x="92" y="22">FROM</text><a xlink:href="../grammar_diagrams#table-name"><rect class="rule" x="146" y="5" width="91" height="25"/><text class="text" x="156" y="22">table_name</text></a><rect class="literal" x="267" y="5" width="60" height="25" rx="7"/><text class="text" x="277" y="22">USING</text><rect class="literal" x="337" y="5" width="90" height="25" rx="7"/><text class="text" x="347" y="22">TIMESTAMP</text><a xlink:href="../grammar_diagrams#timestamp-expression"><rect class="rule" x="437" y="5" width="155" height="25"/><text class="text" x="447" y="22">timestamp_expression</text></a><rect class="literal" x="622" y="5" width="65" height="25" rx="7"/><text class="text" x="632" y="22">WHERE</text><a xlink:href="../grammar_diagrams#where-expression"><rect class="rule" x="697" y="5" width="128" height="25"/><text class="text" x="707" y="22">where_expression</text></a><rect class="literal" x="855" y="5" width="32" height="25" rx="7"/><text class="text" x="865" y="22">IF</text><rect class="literal" x="937" y="5" width="45" height="25" rx="7"/><text class="text" x="947" y="22">NOT</text><rect class="literal" x="1012" y="5" width="64" height="25" rx="7"/><text class="text" x="1022" y="22">EXISTS</text><a xlink:href="../grammar_diagrams#if-expression"><rect class="rule" x="917" y="50" width="98" height="25"/><text class="text" x="927" y="67">if_expression</text></a></svg>

### where_expression
```
where_expression = ( column_name ( '<' | '<=' | '=' | '>=' | '>' | 'IN' | 'NOT IN' ) expression ) { 'AND' ( column_name ( '<' | '<=' | '=' | '>=' | '>' | 'IN' | 'NOT IN' ) expression ) };
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="362" height="245" viewbox="0 0 362 245"><path class="connector" d="M0 52h25m-5 0q-5 0-5-5v-20q0-5 5-5h138m46 0h138q5 0 5 5v20q0 5-5 5m-211 0h30m30 0h53m-93 25q0 5 5 5h5m40 0h28q5 0 5-5m-83 30q0 5 5 5h5m30 0h38q5 0 5-5m-83 30q0 5 5 5h5m40 0h28q5 0 5-5m-83 30q0 5 5 5h5m30 0h38q5 0 5-5m-83 30q0 5 5 5h5m34 0h34q5 0 5-5m-88-145q5 0 5 5v170q0 5 5 5h5m63 0h5q5 0 5-5v-170q0-5 5-5m5 0h10m83 0h25"/><rect class="literal" x="158" y="5" width="46" height="25" rx="7"/><text class="text" x="168" y="22">AND</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="25" y="35" width="106" height="25"/><text class="text" x="35" y="52">column_name</text></a><rect class="literal" x="161" y="35" width="30" height="25" rx="7"/><text class="text" x="171" y="52">&lt;</text><rect class="literal" x="161" y="65" width="40" height="25" rx="7"/><text class="text" x="171" y="82">&lt;=</text><rect class="literal" x="161" y="95" width="30" height="25" rx="7"/><text class="text" x="171" y="112">=</text><rect class="literal" x="161" y="125" width="40" height="25" rx="7"/><text class="text" x="171" y="142">&gt;=</text><rect class="literal" x="161" y="155" width="30" height="25" rx="7"/><text class="text" x="171" y="172">&gt;</text><rect class="literal" x="161" y="185" width="34" height="25" rx="7"/><text class="text" x="171" y="202">IN</text><rect class="literal" x="161" y="215" width="63" height="25" rx="7"/><text class="text" x="171" y="232">NOT IN</text><a xlink:href="../grammar_diagrams#expression"><rect class="rule" x="254" y="35" width="83" height="25"/><text class="text" x="264" y="52">expression</text></a></svg>

### if_expression
```
if_expression = ( column_name ( '<' | '<=' | '=' | '>=' | '>' | 'IN' | 'NOT IN' ) expression ) { 'AND' ( column_name ( '<' | '<=' | '=' | '>=' | '>' | 'IN' | 'NOT IN' ) expression ) };
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="362" height="245" viewbox="0 0 362 245"><path class="connector" d="M0 52h25m-5 0q-5 0-5-5v-20q0-5 5-5h138m46 0h138q5 0 5 5v20q0 5-5 5m-211 0h30m30 0h53m-93 25q0 5 5 5h5m40 0h28q5 0 5-5m-83 30q0 5 5 5h5m30 0h38q5 0 5-5m-83 30q0 5 5 5h5m40 0h28q5 0 5-5m-83 30q0 5 5 5h5m30 0h38q5 0 5-5m-83 30q0 5 5 5h5m34 0h34q5 0 5-5m-88-145q5 0 5 5v170q0 5 5 5h5m63 0h5q5 0 5-5v-170q0-5 5-5m5 0h10m83 0h25"/><rect class="literal" x="158" y="5" width="46" height="25" rx="7"/><text class="text" x="168" y="22">AND</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="25" y="35" width="106" height="25"/><text class="text" x="35" y="52">column_name</text></a><rect class="literal" x="161" y="35" width="30" height="25" rx="7"/><text class="text" x="171" y="52">&lt;</text><rect class="literal" x="161" y="65" width="40" height="25" rx="7"/><text class="text" x="171" y="82">&lt;=</text><rect class="literal" x="161" y="95" width="30" height="25" rx="7"/><text class="text" x="171" y="112">=</text><rect class="literal" x="161" y="125" width="40" height="25" rx="7"/><text class="text" x="171" y="142">&gt;=</text><rect class="literal" x="161" y="155" width="30" height="25" rx="7"/><text class="text" x="171" y="172">&gt;</text><rect class="literal" x="161" y="185" width="34" height="25" rx="7"/><text class="text" x="171" y="202">IN</text><rect class="literal" x="161" y="215" width="63" height="25" rx="7"/><text class="text" x="171" y="232">NOT IN</text><a xlink:href="../grammar_diagrams#expression"><rect class="rule" x="254" y="35" width="83" height="25"/><text class="text" x="264" y="52">expression</text></a></svg>

### insert
```
insert = 'INSERT' 'INTO' table_name '(' column_name { ',' column_name } ')' 'VALUES' '(' expression { ',' expression } ')'
         [ 'IF' ( [ 'NOT' ] 'EXISTS' | if_expression ) ] [ 'USING' using_expression ];
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="743" height="160" viewbox="0 0 743 160"><path class="connector" d="M0 52h5m65 0h10m50 0h10m91 0h10m25 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h46m24 0h46q5 0 5 5v20q0 5-5 5m-5 0h30m25 0h10m68 0h10m25 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h34m24 0h35q5 0 5 5v20q0 5-5 5m-5 0h30m25 0h5m-743 35h25m32 0h50m45 0h20m-80 0q5 0 5 5v8q0 5 5 5h55q5 0 5-5v-8q0-5 5-5m5 0h10m64 0h20m-194 0q5 0 5 5v35q0 5 5 5h5m98 0h66q5 0 5-5v-35q0-5 5-5m5 0h20m-276 0q5 0 5 5v53q0 5 5 5h251q5 0 5-5v-53q0-5 5-5m5 0h30m60 0h10m123 0h20m-228 0q5 0 5 5v8q0 5 5 5h203q5 0 5-5v-8q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="35" width="65" height="25" rx="7"/><text class="text" x="15" y="52">INSERT</text><rect class="literal" x="80" y="35" width="50" height="25" rx="7"/><text class="text" x="90" y="52">INTO</text><a xlink:href="../grammar_diagrams#table-name"><rect class="rule" x="140" y="35" width="91" height="25"/><text class="text" x="150" y="52">table_name</text></a><rect class="literal" x="241" y="35" width="25" height="25" rx="7"/><text class="text" x="251" y="52">(</text><rect class="literal" x="337" y="5" width="24" height="25" rx="7"/><text class="text" x="347" y="22">,</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="296" y="35" width="106" height="25"/><text class="text" x="306" y="52">column_name</text></a><rect class="literal" x="432" y="35" width="25" height="25" rx="7"/><text class="text" x="442" y="52">)</text><rect class="literal" x="467" y="35" width="68" height="25" rx="7"/><text class="text" x="477" y="52">VALUES</text><rect class="literal" x="545" y="35" width="25" height="25" rx="7"/><text class="text" x="555" y="52">(</text><rect class="literal" x="629" y="5" width="24" height="25" rx="7"/><text class="text" x="639" y="22">,</text><a xlink:href="../grammar_diagrams#expression"><rect class="rule" x="600" y="35" width="83" height="25"/><text class="text" x="610" y="52">expression</text></a><rect class="literal" x="713" y="35" width="25" height="25" rx="7"/><text class="text" x="723" y="52">)</text><rect class="literal" x="25" y="70" width="32" height="25" rx="7"/><text class="text" x="35" y="87">IF</text><rect class="literal" x="107" y="70" width="45" height="25" rx="7"/><text class="text" x="117" y="87">NOT</text><rect class="literal" x="182" y="70" width="64" height="25" rx="7"/><text class="text" x="192" y="87">EXISTS</text><a xlink:href="../grammar_diagrams#if-expression"><rect class="rule" x="87" y="115" width="98" height="25"/><text class="text" x="97" y="132">if_expression</text></a><rect class="literal" x="316" y="70" width="60" height="25" rx="7"/><text class="text" x="326" y="87">USING</text><a xlink:href="../grammar_diagrams#using-expression"><rect class="rule" x="386" y="70" width="123" height="25"/><text class="text" x="396" y="87">using_expression</text></a></svg>

### using_expression
```
using_expression = ttl_or_timestamp_expression { 'AND' ttl_or_timestamp_expression };
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="246" height="65" viewbox="0 0 246 65"><path class="connector" d="M0 52h25m-5 0q-5 0-5-5v-20q0-5 5-5h80m46 0h80q5 0 5 5v20q0 5-5 5m-5 0h25"/><rect class="literal" x="100" y="5" width="46" height="25" rx="7"/><text class="text" x="110" y="22">AND</text><a xlink:href="../grammar_diagrams#ttl-or-timestamp-expression"><rect class="rule" x="25" y="35" width="196" height="25"/><text class="text" x="35" y="52">ttl_or_timestamp_expression</text></a></svg>

### ttl_or_timestamp_expression
```
ttl_or_timestamp_expression = 'TTL' ttl_expression | 'TIMESTAMP' timestamp_expression;
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="305" height="65" viewbox="0 0 305 65"><path class="connector" d="M0 22h25m41 0h10m104 0h120m-290 0q5 0 5 5v20q0 5 5 5h5m90 0h10m155 0h5q5 0 5-5v-20q0-5 5-5m5 0h5"/><rect class="literal" x="25" y="5" width="41" height="25" rx="7"/><text class="text" x="35" y="22">TTL</text><a xlink:href="../grammar_diagrams#ttl-expression"><rect class="rule" x="76" y="5" width="104" height="25"/><text class="text" x="86" y="22">ttl_expression</text></a><rect class="literal" x="25" y="35" width="90" height="25" rx="7"/><text class="text" x="35" y="52">TIMESTAMP</text><a xlink:href="../grammar_diagrams#timestamp-expression"><rect class="rule" x="125" y="35" width="155" height="25"/><text class="text" x="135" y="52">timestamp_expression</text></a></svg>

### expression
```
expression = '<expression>';
```
- See [Expressions](../#expressions).

### select
```
select = 'SELECT' [ 'DISTINCT' ] ( '*' | column_name { ',' column_name } ) 'FROM' table_name
         [ 'WHERE' where_expression ] [ 'ORDER BY' order_expression ] [ 'LIMIT' limit_expression ] [ 'OFFSET' offset_expression ];
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="997" height="145" viewbox="0 0 997 145"><path class="connector" d="M0 22h5m66 0h30m78 0h20m-113 0q5 0 5 5v8q0 5 5 5h88q5 0 5-5v-8q0-5 5-5m5 0h30m28 0h138m-181 0q5 0 5 5v50q0 5 5 5h25m-5 0q-5 0-5-5v-20q0-5 5-5h46m24 0h46q5 0 5 5v20q0 5-5 5m-5 0h25q5 0 5-5v-50q0-5 5-5m5 0h10m54 0h10m91 0h5m-565 95h25m65 0h10m128 0h20m-238 0q5 0 5 5v8q0 5 5 5h213q5 0 5-5v-8q0-5 5-5m5 0h30m81 0h10m122 0h20m-248 0q5 0 5 5v8q0 5 5 5h223q5 0 5-5v-8q0-5 5-5m5 0h30m54 0h10m117 0h20m-216 0q5 0 5 5v8q0 5 5 5h191q5 0 5-5v-8q0-5 5-5m5 0h30m66 0h10m124 0h20m-235 0q5 0 5 5v8q0 5 5 5h210q5 0 5-5v-8q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="5" width="66" height="25" rx="7"/><text class="text" x="15" y="22">SELECT</text><rect class="literal" x="101" y="5" width="78" height="25" rx="7"/><text class="text" x="111" y="22">DISTINCT</text><rect class="literal" x="229" y="5" width="28" height="25" rx="7"/><text class="text" x="239" y="22">*</text><rect class="literal" x="290" y="35" width="24" height="25" rx="7"/><text class="text" x="300" y="52">,</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="249" y="65" width="106" height="25"/><text class="text" x="259" y="82">column_name</text></a><rect class="literal" x="405" y="5" width="54" height="25" rx="7"/><text class="text" x="415" y="22">FROM</text><a xlink:href="../grammar_diagrams#table-name"><rect class="rule" x="469" y="5" width="91" height="25"/><text class="text" x="479" y="22">table_name</text></a><rect class="literal" x="25" y="100" width="65" height="25" rx="7"/><text class="text" x="35" y="117">WHERE</text><a xlink:href="../grammar_diagrams#where-expression"><rect class="rule" x="100" y="100" width="128" height="25"/><text class="text" x="110" y="117">where_expression</text></a><rect class="literal" x="278" y="100" width="81" height="25" rx="7"/><text class="text" x="288" y="117">ORDER BY</text><a xlink:href="../grammar_diagrams#order-expression"><rect class="rule" x="369" y="100" width="122" height="25"/><text class="text" x="379" y="117">order_expression</text></a><rect class="literal" x="541" y="100" width="54" height="25" rx="7"/><text class="text" x="551" y="117">LIMIT</text><a xlink:href="../grammar_diagrams#limit-expression"><rect class="rule" x="605" y="100" width="117" height="25"/><text class="text" x="615" y="117">limit_expression</text></a><rect class="literal" x="772" y="100" width="66" height="25" rx="7"/><text class="text" x="782" y="117">OFFSET</text><a xlink:href="../grammar_diagrams#offset-expression"><rect class="rule" x="848" y="100" width="124" height="25"/><text class="text" x="858" y="117">offset_expression</text></a></svg>

### order_expression
```
order_expression = '(' ( column_name [ 'ASC' | 'DESC' ] ) { ',' ( column_name [ 'ASC' | 'DESC' ] ) } ')';
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="329" height="100" viewbox="0 0 329 100"><path class="connector" d="M0 52h5m25 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h97m24 0h98q5 0 5 5v20q0 5-5 5m-108 0h30m44 0h29m-83 25q0 5 5 5h5m53 0h5q5 0 5-5m-78-25q5 0 5 5v33q0 5 5 5h63q5 0 5-5v-33q0-5 5-5m5 0h30m25 0h5"/><rect class="literal" x="5" y="35" width="25" height="25" rx="7"/><text class="text" x="15" y="52">(</text><rect class="literal" x="152" y="5" width="24" height="25" rx="7"/><text class="text" x="162" y="22">,</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="60" y="35" width="106" height="25"/><text class="text" x="70" y="52">column_name</text></a><rect class="literal" x="196" y="35" width="44" height="25" rx="7"/><text class="text" x="206" y="52">ASC</text><rect class="literal" x="196" y="65" width="53" height="25" rx="7"/><text class="text" x="206" y="82">DESC</text><rect class="literal" x="299" y="35" width="25" height="25" rx="7"/><text class="text" x="309" y="52">)</text></svg>

### update
```
update = 'UPDATE' table_name [ 'USING' using_expression ) ] 'SET' assignment { ',' assignment }
         'WHERE' where_expression [ 'IF' ( [ 'NOT' ] 'EXISTS' | if_expression ) ];
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="614" height="175" viewbox="0 0 614 175"><path class="connector" d="M0 52h5m68 0h10m91 0h30m60 0h10m123 0h20m-228 0q5 0 5 5v8q0 5 5 5h203q5 0 5-5v-8q0-5 5-5m5 0h10m43 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h37m24 0h38q5 0 5 5v20q0 5-5 5m-5 0h25m-614 50h5m65 0h10m128 0h30m32 0h50m45 0h20m-80 0q5 0 5 5v8q0 5 5 5h55q5 0 5-5v-8q0-5 5-5m5 0h10m64 0h20m-194 0q5 0 5 5v35q0 5 5 5h5m98 0h66q5 0 5-5v-35q0-5 5-5m5 0h20m-276 0q5 0 5 5v53q0 5 5 5h251q5 0 5-5v-53q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="35" width="68" height="25" rx="7"/><text class="text" x="15" y="52">UPDATE</text><a xlink:href="../grammar_diagrams#table-name"><rect class="rule" x="83" y="35" width="91" height="25"/><text class="text" x="93" y="52">table_name</text></a><rect class="literal" x="204" y="35" width="60" height="25" rx="7"/><text class="text" x="214" y="52">USING</text><a xlink:href="../grammar_diagrams#using-expression"><rect class="rule" x="274" y="35" width="123" height="25"/><text class="text" x="284" y="52">using_expression</text></a><rect class="literal" x="427" y="35" width="43" height="25" rx="7"/><text class="text" x="437" y="52">SET</text><rect class="literal" x="532" y="5" width="24" height="25" rx="7"/><text class="text" x="542" y="22">,</text><a xlink:href="../grammar_diagrams#assignment"><rect class="rule" x="500" y="35" width="89" height="25"/><text class="text" x="510" y="52">assignment</text></a><rect class="literal" x="5" y="85" width="65" height="25" rx="7"/><text class="text" x="15" y="102">WHERE</text><a xlink:href="../grammar_diagrams#where-expression"><rect class="rule" x="80" y="85" width="128" height="25"/><text class="text" x="90" y="102">where_expression</text></a><rect class="literal" x="238" y="85" width="32" height="25" rx="7"/><text class="text" x="248" y="102">IF</text><rect class="literal" x="320" y="85" width="45" height="25" rx="7"/><text class="text" x="330" y="102">NOT</text><rect class="literal" x="395" y="85" width="64" height="25" rx="7"/><text class="text" x="405" y="102">EXISTS</text><a xlink:href="../grammar_diagrams#if-expression"><rect class="rule" x="300" y="130" width="98" height="25"/><text class="text" x="310" y="147">if_expression</text></a></svg>

### transaction_block
```
transaction_block = 'BEGIN' 'TRANSACTION'
                         ( insert | update | delete ) ';'
                         [ ( insert | update | delete ) ';' ...]
                    'END' 'TRANSACTION' ';'
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="207" height="180" viewbox="0 0 207 180"><path class="connector" d="M0 22h5m59 0h10m106 0h5m-185 50h25m-5 0q-5 0-5-5v-17q0-5 5-5h146q5 0 5 5v17q0 5-5 5m-141 0h20m54 0h27m-91 25q0 5 5 5h5m61 0h5q5 0 5-5m-86-25q5 0 5 5v50q0 5 5 5h5m56 0h10q5 0 5-5v-50q0-5 5-5m5 0h10m25 0h25m-186 95h5m46 0h10m106 0h10m25 0h5"/><rect class="literal" x="5" y="5" width="59" height="25" rx="7"/><text class="text" x="15" y="22">BEGIN</text><rect class="literal" x="74" y="5" width="106" height="25" rx="7"/><text class="text" x="84" y="22">TRANSACTION</text><a xlink:href="../grammar_diagrams#insert"><rect class="rule" x="45" y="55" width="54" height="25"/><text class="text" x="55" y="72">insert</text></a><a xlink:href="../grammar_diagrams#update"><rect class="rule" x="45" y="85" width="61" height="25"/><text class="text" x="55" y="102">update</text></a><a xlink:href="../grammar_diagrams#delete"><rect class="rule" x="45" y="115" width="56" height="25"/><text class="text" x="55" y="132">delete</text></a><rect class="literal" x="136" y="55" width="25" height="25" rx="7"/><text class="text" x="146" y="72">;</text><rect class="literal" x="5" y="150" width="46" height="25" rx="7"/><text class="text" x="15" y="167">END</text><rect class="literal" x="61" y="150" width="106" height="25" rx="7"/><text class="text" x="71" y="167">TRANSACTION</text><rect class="literal" x="177" y="150" width="25" height="25" rx="7"/><text class="text" x="187" y="167">;</text></svg>

### truncate
```
truncate = 'TRUNCATE' [ 'TABLE' ] table_name;
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="303" height="50" viewbox="0 0 303 50"><path class="connector" d="M0 22h5m84 0h30m58 0h20m-93 0q5 0 5 5v8q0 5 5 5h68q5 0 5-5v-8q0-5 5-5m5 0h10m91 0h5"/><rect class="literal" x="5" y="5" width="84" height="25" rx="7"/><text class="text" x="15" y="22">TRUNCATE</text><rect class="literal" x="119" y="5" width="58" height="25" rx="7"/><text class="text" x="129" y="22">TABLE</text><a xlink:href="../grammar_diagrams#table-name"><rect class="rule" x="207" y="5" width="91" height="25"/><text class="text" x="217" y="22">table_name</text></a></svg>

### assignment
```
assignment = column_name '=' expression;
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="249" height="35" viewbox="0 0 249 35"><path class="connector" d="M0 22h5m106 0h10m30 0h10m83 0h5"/><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="5" y="5" width="106" height="25"/><text class="text" x="15" y="22">column_name</text></a><rect class="literal" x="121" y="5" width="30" height="25" rx="7"/><text class="text" x="131" y="22">=</text><a xlink:href="../grammar_diagrams#expression"><rect class="rule" x="161" y="5" width="83" height="25"/><text class="text" x="171" y="22">expression</text></a></svg>

## Literals

### ttl_expression
```
ttl_expression = '<Integer Literal>';
```
- See also [Expressions](../#expressions).

### timestamp_expression
```
timestamp_expression = '<Integer Literal>';
```
- See also [Expressions](../#expressions).

### limit_expression
```
limit_expression = '<Integer Literal>';
```
- See also [Expressions](../#expressions).

### offset_expression
```
offset_expression = '<Integer Literal>';
```
- See also [Expressions](../#expressions).

### keyspace_name
```
keyspace_name = '<Text Literal>';
```

### property_name
```
property_name = '<Text Literal>';
```

### property_literal
```
property_literal = '<Text Literal>';
```

### table_name
```
table_name = [ keyspace_name '.' ] '<Text Literal>';
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="317" height="50" viewbox="0 0 317 50"><path class="connector" d="M0 22h25m116 0h10m24 0h20m-185 0q5 0 5 5v8q0 5 5 5h160q5 0 5-5v-8q0-5 5-5m5 0h10m107 0h5"/><a xlink:href="../grammar_diagrams#keyspace-name"><rect class="rule" x="25" y="5" width="116" height="25"/><text class="text" x="35" y="22">keyspace_name</text></a><rect class="literal" x="151" y="5" width="24" height="25" rx="7"/><text class="text" x="161" y="22">.</text><rect class="literal" x="205" y="5" width="107" height="25" rx="7"/><text class="text" x="215" y="22">&lt;Text Literal&gt;</text></svg>

### index_name
```
index_name = [ keyspace_name '.' ] '<Text Literal>';
```

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="317" height="50" viewbox="0 0 317 50"><path class="connector" d="M0 22h25m116 0h10m24 0h20m-185 0q5 0 5 5v8q0 5 5 5h160q5 0 5-5v-8q0-5 5-5m5 0h10m107 0h5"/><a xlink:href="../grammar_diagrams#keyspace-name"><rect class="rule" x="25" y="5" width="116" height="25"/><text class="text" x="35" y="22">keyspace_name</text></a><rect class="literal" x="151" y="5" width="24" height="25" rx="7"/><text class="text" x="161" y="22">.</text><rect class="literal" x="205" y="5" width="107" height="25" rx="7"/><text class="text" x="215" y="22">&lt;Text Literal&gt;</text></svg>

### column_name
```
column_name = '<Text Literal>';
```

### type_name
```
type_name = [ keyspace_name '.' ] '<Text Literal>';
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="317" height="50" viewbox="0 0 317 50"><path class="connector" d="M0 22h25m116 0h10m24 0h20m-185 0q5 0 5 5v8q0 5 5 5h160q5 0 5-5v-8q0-5 5-5m5 0h10m107 0h5"/><a xlink:href="../grammar_diagrams#keyspace-name"><rect class="rule" x="25" y="5" width="116" height="25"/><text class="text" x="35" y="22">keyspace_name</text></a><rect class="literal" x="151" y="5" width="24" height="25" rx="7"/><text class="text" x="161" y="22">.</text><rect class="literal" x="205" y="5" width="107" height="25" rx="7"/><text class="text" x="215" y="22">&lt;Text Literal&gt;</text></svg>

### field_name
```
field_name = '<Text Literal>';
```

### alter_role
```
alter_role = 'ALTER' 'ROLE' role_name 'WITH' role_property { 'AND' role_property };
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="437" height="65" viewbox="0 0 437 65"><path class="connector" d="M0 52h5m58 0h10m52 0h10m84 0h10m53 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h32m46 0h32q5 0 5 5v20q0 5-5 5m-5 0h25"/><rect class="literal" x="5" y="35" width="58" height="25" rx="7"/><text class="text" x="15" y="52">ALTER</text><rect class="literal" x="73" y="35" width="52" height="25" rx="7"/><text class="text" x="83" y="52">ROLE</text><a xlink:href="../grammar_diagrams#role-name"><rect class="rule" x="135" y="35" width="84" height="25"/><text class="text" x="145" y="52">role_name</text></a><rect class="literal" x="229" y="35" width="53" height="25" rx="7"/><text class="text" x="239" y="52">WITH</text><rect class="literal" x="339" y="5" width="46" height="25" rx="7"/><text class="text" x="349" y="22">AND</text><a xlink:href="../grammar_diagrams#role-property"><rect class="rule" x="312" y="35" width="100" height="25"/><text class="text" x="322" y="52">role_property</text></a></svg>

### create_role
```
create_role = 'CREATE' 'ROLE' role_name [ 'WITH' role_property { 'AND' role_property } ];
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="486" height="80" viewbox="0 0 486 80"><path class="connector" d="M0 52h5m67 0h10m52 0h10m84 0h30m53 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h32m46 0h32q5 0 5 5v20q0 5-5 5m-5 0h40m-238 0q5 0 5 5v8q0 5 5 5h213q5 0 5-5v-8q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="35" width="67" height="25" rx="7"/><text class="text" x="15" y="52">CREATE</text><rect class="literal" x="82" y="35" width="52" height="25" rx="7"/><text class="text" x="92" y="52">ROLE</text><a xlink:href="../grammar_diagrams#role-name"><rect class="rule" x="144" y="35" width="84" height="25"/><text class="text" x="154" y="52">role_name</text></a><rect class="literal" x="258" y="35" width="53" height="25" rx="7"/><text class="text" x="268" y="52">WITH</text><rect class="literal" x="368" y="5" width="46" height="25" rx="7"/><text class="text" x="378" y="22">AND</text><a xlink:href="../grammar_diagrams#role-property"><rect class="rule" x="341" y="35" width="100" height="25"/><text class="text" x="351" y="52">role_property</text></a></svg>

### role_property
```
role_property = 'PASSWORD' '=' '<Text Literal>' | 'LOGIN' '=' '<Boolean Literal>' | 'SUPERUSER' '=' '<Boolean Literal>';
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="321" height="95" viewbox="0 0 321 95"><path class="connector" d="M0 22h25m89 0h10m30 0h10m107 0h45m-301 25q0 5 5 5h5m59 0h10m30 0h10m128 0h39q5 0 5-5m-296-25q5 0 5 5v50q0 5 5 5h5m93 0h10m30 0h10m128 0h5q5 0 5-5v-50q0-5 5-5m5 0h5"/><rect class="literal" x="25" y="5" width="89" height="25" rx="7"/><text class="text" x="35" y="22">PASSWORD</text><rect class="literal" x="124" y="5" width="30" height="25" rx="7"/><text class="text" x="134" y="22">=</text><rect class="literal" x="164" y="5" width="107" height="25" rx="7"/><text class="text" x="174" y="22">&lt;Text Literal&gt;</text><rect class="literal" x="25" y="35" width="59" height="25" rx="7"/><text class="text" x="35" y="52">LOGIN</text><rect class="literal" x="94" y="35" width="30" height="25" rx="7"/><text class="text" x="104" y="52">=</text><rect class="literal" x="134" y="35" width="128" height="25" rx="7"/><text class="text" x="144" y="52">&lt;Boolean Literal&gt;</text><rect class="literal" x="25" y="65" width="93" height="25" rx="7"/><text class="text" x="35" y="82">SUPERUSER</text><rect class="literal" x="128" y="65" width="30" height="25" rx="7"/><text class="text" x="138" y="82">=</text><rect class="literal" x="168" y="65" width="128" height="25" rx="7"/><text class="text" x="178" y="82">&lt;Boolean Literal&gt;</text></svg>

### drop_role
```
drop_role = 'DROP' 'ROLE' [ 'IF' 'EXISTS' ] role_name;
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="375" height="50" viewbox="0 0 375 50"><path class="connector" d="M0 22h5m53 0h10m52 0h30m32 0h10m64 0h20m-141 0q5 0 5 5v8q0 5 5 5h116q5 0 5-5v-8q0-5 5-5m5 0h10m84 0h5"/><rect class="literal" x="5" y="5" width="53" height="25" rx="7"/><text class="text" x="15" y="22">DROP</text><rect class="literal" x="68" y="5" width="52" height="25" rx="7"/><text class="text" x="78" y="22">ROLE</text><rect class="literal" x="150" y="5" width="32" height="25" rx="7"/><text class="text" x="160" y="22">IF</text><rect class="literal" x="192" y="5" width="64" height="25" rx="7"/><text class="text" x="202" y="22">EXISTS</text><a xlink:href="../grammar_diagrams#role-name"><rect class="rule" x="286" y="5" width="84" height="25"/><text class="text" x="296" y="22">role_name</text></a></svg>

### grant_role
```
grant_role = 'GRANT' role_name 'TO' role_name;
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="305" height="35" viewbox="0 0 305 35"><path class="connector" d="M0 22h5m61 0h10m84 0h10m36 0h10m84 0h5"/><rect class="literal" x="5" y="5" width="61" height="25" rx="7"/><text class="text" x="15" y="22">GRANT</text><a xlink:href="../grammar_diagrams#role-name"><rect class="rule" x="76" y="5" width="84" height="25"/><text class="text" x="86" y="22">role_name</text></a><rect class="literal" x="170" y="5" width="36" height="25" rx="7"/><text class="text" x="180" y="22">TO</text><a xlink:href="../grammar_diagrams#role-name"><rect class="rule" x="216" y="5" width="84" height="25"/><text class="text" x="226" y="22">role_name</text></a></svg>

### revoke_role
```
revoke_role = 'REVOKE' role_name 'FROM' role_name;
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="331" height="35" viewbox="0 0 331 35"><path class="connector" d="M0 22h5m69 0h10m84 0h10m54 0h10m84 0h5"/><rect class="literal" x="5" y="5" width="69" height="25" rx="7"/><text class="text" x="15" y="22">REVOKE</text><a xlink:href="../grammar_diagrams#role-name"><rect class="rule" x="84" y="5" width="84" height="25"/><text class="text" x="94" y="22">role_name</text></a><rect class="literal" x="178" y="5" width="54" height="25" rx="7"/><text class="text" x="188" y="22">FROM</text><a xlink:href="../grammar_diagrams#role-name"><rect class="rule" x="242" y="5" width="84" height="25"/><text class="text" x="252" y="22">role_name</text></a></svg>

### grant_permission
```
grant_permission = 'GRANT' ( all_permissions | permission ) 'ON' resource 'TO' role_name;
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="501" height="65" viewbox="0 0 501 65"><path class="connector" d="M0 22h5m61 0h30m111 0h20m-146 0q5 0 5 5v20q0 5 5 5h5m84 0h32q5 0 5-5v-20q0-5 5-5m5 0h10m38 0h10m71 0h10m36 0h10m84 0h5"/><rect class="literal" x="5" y="5" width="61" height="25" rx="7"/><text class="text" x="15" y="22">GRANT</text><a xlink:href="../grammar_diagrams#all-permissions"><rect class="rule" x="96" y="5" width="111" height="25"/><text class="text" x="106" y="22">all_permissions</text></a><a xlink:href="../grammar_diagrams#permission"><rect class="rule" x="96" y="35" width="84" height="25"/><text class="text" x="106" y="52">permission</text></a><rect class="literal" x="237" y="5" width="38" height="25" rx="7"/><text class="text" x="247" y="22">ON</text><a xlink:href="../grammar_diagrams#resource"><rect class="rule" x="285" y="5" width="71" height="25"/><text class="text" x="295" y="22">resource</text></a><rect class="literal" x="366" y="5" width="36" height="25" rx="7"/><text class="text" x="376" y="22">TO</text><a xlink:href="../grammar_diagrams#role-name"><rect class="rule" x="412" y="5" width="84" height="25"/><text class="text" x="422" y="22">role_name</text></a></svg>

### revoke_permission
```
revoke_permission = 'REVOKE' ( all_permissions | permission ) 'ON' resource 'FROM' role_name;
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="527" height="65" viewbox="0 0 527 65"><path class="connector" d="M0 22h5m69 0h30m111 0h20m-146 0q5 0 5 5v20q0 5 5 5h5m84 0h32q5 0 5-5v-20q0-5 5-5m5 0h10m38 0h10m71 0h10m54 0h10m84 0h5"/><rect class="literal" x="5" y="5" width="69" height="25" rx="7"/><text class="text" x="15" y="22">REVOKE</text><a xlink:href="../grammar_diagrams#all-permissions"><rect class="rule" x="104" y="5" width="111" height="25"/><text class="text" x="114" y="22">all_permissions</text></a><a xlink:href="../grammar_diagrams#permission"><rect class="rule" x="104" y="35" width="84" height="25"/><text class="text" x="114" y="52">permission</text></a><rect class="literal" x="245" y="5" width="38" height="25" rx="7"/><text class="text" x="255" y="22">ON</text><a xlink:href="../grammar_diagrams#resource"><rect class="rule" x="293" y="5" width="71" height="25"/><text class="text" x="303" y="22">resource</text></a><rect class="literal" x="374" y="5" width="54" height="25" rx="7"/><text class="text" x="384" y="22">FROM</text><a xlink:href="../grammar_diagrams#role-name"><rect class="rule" x="438" y="5" width="84" height="25"/><text class="text" x="448" y="22">role_name</text></a></svg>

### all_permissions
```
all_permissions = 'ALL' [ 'PERMISSIONS' ];
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="207" height="50" viewbox="0 0 207 50"><path class="connector" d="M0 22h5m42 0h30m105 0h20m-140 0q5 0 5 5v8q0 5 5 5h115q5 0 5-5v-8q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="5" width="42" height="25" rx="7"/><text class="text" x="15" y="22">ALL</text><rect class="literal" x="77" y="5" width="105" height="25" rx="7"/><text class="text" x="87" y="22">PERMISSIONS</text></svg>

### role_name
```
role_name = '<Text Literal>';
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="117" height="35" viewbox="0 0 117 35"><path class="connector" d="M0 22h5m107 0h5"/><rect class="literal" x="5" y="5" width="107" height="25" rx="7"/><text class="text" x="15" y="22">&lt;Text Literal&gt;</text></svg>

### permission
```
permission = ( 'CREATE' | 'ALTER' | 'DROP' | 'SELECT' | 'MODIFY' | 'AUTHORIZE' | 'DESCRIBE' | 'EXECUTE' ) [ 'PERMISSION' ];
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="288" height="245" viewbox="0 0 288 245"><path class="connector" d="M0 22h25m67 0h44m-121 25q0 5 5 5h5m58 0h38q5 0 5-5m-111 30q0 5 5 5h5m53 0h43q5 0 5-5m-111 30q0 5 5 5h5m66 0h30q5 0 5-5m-111 30q0 5 5 5h5m67 0h29q5 0 5-5m-111 30q0 5 5 5h5m91 0h5q5 0 5-5m-111 30q0 5 5 5h5m82 0h14q5 0 5-5m-116-175q5 0 5 5v200q0 5 5 5h5m76 0h20q5 0 5-5v-200q0-5 5-5m5 0h30m97 0h20m-132 0q5 0 5 5v8q0 5 5 5h107q5 0 5-5v-8q0-5 5-5m5 0h5"/><rect class="literal" x="25" y="5" width="67" height="25" rx="7"/><text class="text" x="35" y="22">CREATE</text><rect class="literal" x="25" y="35" width="58" height="25" rx="7"/><text class="text" x="35" y="52">ALTER</text><rect class="literal" x="25" y="65" width="53" height="25" rx="7"/><text class="text" x="35" y="82">DROP</text><rect class="literal" x="25" y="95" width="66" height="25" rx="7"/><text class="text" x="35" y="112">SELECT</text><rect class="literal" x="25" y="125" width="67" height="25" rx="7"/><text class="text" x="35" y="142">MODIFY</text><rect class="literal" x="25" y="155" width="91" height="25" rx="7"/><text class="text" x="35" y="172">AUTHORIZE</text><rect class="literal" x="25" y="185" width="82" height="25" rx="7"/><text class="text" x="35" y="202">DESCRIBE</text><rect class="literal" x="25" y="215" width="76" height="25" rx="7"/><text class="text" x="35" y="232">EXECUTE</text><rect class="literal" x="166" y="5" width="97" height="25" rx="7"/><text class="text" x="176" y="22">PERMISSION</text></svg>

### resource
```
resource = 'ALL' ( 'KEYSPACES' | 'ROLES' ) | 'KEYSPACE' keyspace_name | [ 'TABLE' ] table_name | 'ROLE' role_name;
```
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="258" height="170" viewbox="0 0 258 170"><path class="connector" d="M0 22h25m42 0h30m90 0h20m-125 0q5 0 5 5v20q0 5 5 5h5m60 0h35q5 0 5-5v-20q0-5 5-5m5 0h46m-238 55q0 5 5 5h5m82 0h10m116 0h5q5 0 5-5m-228 30q0 5 5 5h25m58 0h20m-93 0q5 0 5 5v8q0 5 5 5h68q5 0 5-5v-8q0-5 5-5m5 0h10m91 0h14q5 0 5-5m-233-85q5 0 5 5v125q0 5 5 5h5m52 0h10m84 0h67q5 0 5-5v-125q0-5 5-5m5 0h5"/><rect class="literal" x="25" y="5" width="42" height="25" rx="7"/><text class="text" x="35" y="22">ALL</text><rect class="literal" x="97" y="5" width="90" height="25" rx="7"/><text class="text" x="107" y="22">KEYSPACES</text><rect class="literal" x="97" y="35" width="60" height="25" rx="7"/><text class="text" x="107" y="52">ROLES</text><rect class="literal" x="25" y="65" width="82" height="25" rx="7"/><text class="text" x="35" y="82">KEYSPACE</text><a xlink:href="../grammar_diagrams#keyspace-name"><rect class="rule" x="117" y="65" width="116" height="25"/><text class="text" x="127" y="82">keyspace_name</text></a><rect class="literal" x="45" y="95" width="58" height="25" rx="7"/><text class="text" x="55" y="112">TABLE</text><a xlink:href="../grammar_diagrams#table-name"><rect class="rule" x="133" y="95" width="91" height="25"/><text class="text" x="143" y="112">table_name</text></a><rect class="literal" x="25" y="140" width="52" height="25" rx="7"/><text class="text" x="35" y="157">ROLE</text><a xlink:href="../grammar_diagrams#role-name"><rect class="rule" x="87" y="140" width="84" height="25"/><text class="text" x="97" y="157">role_name</text></a></svg>
