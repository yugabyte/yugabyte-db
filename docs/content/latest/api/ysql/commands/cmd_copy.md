---
title: COPY
linkTitle: COPY
summary: COPY
description: COPY
menu:
  latest:
    identifier: api-ysql-commands-copy
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/commands/cmd_copy
isTocNested: true
showAsideToc: true
---

## Synopsis

`COPY` command transfers data between tables and files. `COPY TO` copies from tables to files. `COPY FROM` copies from files to tables. `COPY` outputs the number of rows that were copied.

## Syntax

### Diagrams
#### copy_from
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="605" height="199" viewbox="0 0 605 199"><path class="connector" d="M0 50h5m52 0h10m93 0h30m25 0h30m-5 0q-5 0-5-5v-19q0-5 5-5h46m24 0h46q5 0 5 5v19q0 5-5 5m-5 0h30m25 0h20m-251 0q5 0 5 5v8q0 5 5 5h226q5 0 5-5v-8q0-5 5-5m5 0h5m-431 78h5m54 0h30m73 0h118m-201 24q0 5 5 5h5m80 0h10m81 0h5q5 0 5-5m-196-24q5 0 5 5v48q0 5 5 5h5m56 0h120q5 0 5-5v-48q0-5 5-5m5 0h50m50 0h20m-85 0q5 0 5 5v8q0 5 5 5h60q5 0 5-5v-8q0-5 5-5m5 0h10m25 0h30m-5 0q-5 0-5-5v-19q0-5 5-5h23m24 0h23q5 0 5 5v19q0 5-5 5m-5 0h30m25 0h20m-305 0q5 0 5 5v23q0 5 5 5h280q5 0 5-5v-23q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="34" width="52" height="24" rx="7"/><text class="text" x="15" y="50">COPY</text><a xlink:href="../grammar_diagrams#table-name"><rect class="rule" x="67" y="34" width="93" height="24"/><text class="text" x="77" y="50">table_name</text></a><rect class="literal" x="190" y="34" width="25" height="24" rx="7"/><text class="text" x="200" y="50">(</text><rect class="literal" x="286" y="5" width="24" height="24" rx="7"/><text class="text" x="296" y="21">,</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="245" y="34" width="106" height="24"/><text class="text" x="255" y="50">column_name</text></a><rect class="literal" x="381" y="34" width="25" height="24" rx="7"/><text class="text" x="391" y="50">)</text><rect class="literal" x="5" y="112" width="54" height="24" rx="7"/><text class="text" x="15" y="128">FROM</text><rect class="literal" x="89" y="112" width="73" height="24" rx="7"/><text class="text" x="99" y="128">filename</text><rect class="literal" x="89" y="141" width="80" height="24" rx="7"/><text class="text" x="99" y="157">PROGRAM</text><rect class="literal" x="179" y="141" width="81" height="24" rx="7"/><text class="text" x="189" y="157">command</text><rect class="literal" x="89" y="170" width="56" height="24" rx="7"/><text class="text" x="99" y="186">STDIN</text><rect class="literal" x="330" y="112" width="50" height="24" rx="7"/><text class="text" x="340" y="128">WITH</text><rect class="literal" x="410" y="112" width="25" height="24" rx="7"/><text class="text" x="420" y="128">(</text><rect class="literal" x="483" y="83" width="24" height="24" rx="7"/><text class="text" x="493" y="99">,</text><a xlink:href="../grammar_diagrams#option"><rect class="rule" x="465" y="112" width="60" height="24"/><text class="text" x="475" y="128">option</text></a><rect class="literal" x="555" y="112" width="25" height="24" rx="7"/><text class="text" x="565" y="128">)</text></svg>

#### copy_to
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="587" height="199" viewbox="0 0 587 199"><path class="connector" d="M0 21h5m52 0h30m93 0h30m25 0h10m113 0h10m25 0h20m-218 0q5 0 5 5v8q0 5 5 5h193q5 0 5-5v-8q0-5 5-5m5 0h20m-361 0q5 0 5 5v34q0 5 5 5h5m25 0h10m55 0h10m25 0h206q5 0 5-5v-34q0-5 5-5m5 0h5m-438 107h5m36 0h30m73 0h118m-201 24q0 5 5 5h5m80 0h10m81 0h5q5 0 5-5m-196-24q5 0 5 5v48q0 5 5 5h5m69 0h107q5 0 5-5v-48q0-5 5-5m5 0h50m50 0h20m-85 0q5 0 5 5v8q0 5 5 5h60q5 0 5-5v-8q0-5 5-5m5 0h10m25 0h30m-5 0q-5 0-5-5v-19q0-5 5-5h23m24 0h23q5 0 5 5v19q0 5-5 5m-5 0h30m25 0h20m-305 0q5 0 5 5v23q0 5 5 5h280q5 0 5-5v-23q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="5" width="52" height="24" rx="7"/><text class="text" x="15" y="21">COPY</text><a xlink:href="../grammar_diagrams#table-name"><rect class="rule" x="87" y="5" width="93" height="24"/><text class="text" x="97" y="21">table_name</text></a><rect class="literal" x="210" y="5" width="25" height="24" rx="7"/><text class="text" x="220" y="21">(</text><a xlink:href="../grammar_diagrams#column-names"><rect class="rule" x="245" y="5" width="113" height="24"/><text class="text" x="255" y="21">column_names</text></a><rect class="literal" x="368" y="5" width="25" height="24" rx="7"/><text class="text" x="378" y="21">)</text><rect class="literal" x="87" y="49" width="25" height="24" rx="7"/><text class="text" x="97" y="65">(</text><a xlink:href="../grammar_diagrams#query"><rect class="rule" x="122" y="49" width="55" height="24"/><text class="text" x="132" y="65">query</text></a><rect class="literal" x="187" y="49" width="25" height="24" rx="7"/><text class="text" x="197" y="65">)</text><rect class="literal" x="5" y="112" width="36" height="24" rx="7"/><text class="text" x="15" y="128">TO</text><rect class="literal" x="71" y="112" width="73" height="24" rx="7"/><text class="text" x="81" y="128">filename</text><rect class="literal" x="71" y="141" width="80" height="24" rx="7"/><text class="text" x="81" y="157">PROGRAM</text><rect class="literal" x="161" y="141" width="81" height="24" rx="7"/><text class="text" x="171" y="157">command</text><rect class="literal" x="71" y="170" width="69" height="24" rx="7"/><text class="text" x="81" y="186">STDOUT</text><rect class="literal" x="312" y="112" width="50" height="24" rx="7"/><text class="text" x="322" y="128">WITH</text><rect class="literal" x="392" y="112" width="25" height="24" rx="7"/><text class="text" x="402" y="128">(</text><rect class="literal" x="465" y="83" width="24" height="24" rx="7"/><text class="text" x="475" y="99">,</text><a xlink:href="../grammar_diagrams#option"><rect class="rule" x="447" y="112" width="60" height="24"/><text class="text" x="457" y="128">option</text></a><rect class="literal" x="537" y="112" width="25" height="24" rx="7"/><text class="text" x="547" y="128">)</text></svg>

#### copy_option
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="391" height="427" viewbox="0 0 391 427"><path class="connector" d="M0 21h25m69 0h10m102 0h180m-371 24q0 5 5 5h5m49 0h30m71 0h20m-106 0q5 0 5 5v8q0 5 5 5h81q5 0 5-5v-8q0-5 5-5m5 0h176q5 0 5-5m-361 44q0 5 5 5h5m68 0h30m71 0h20m-106 0q5 0 5 5v8q0 5 5 5h81q5 0 5-5v-8q0-5 5-5m5 0h157q5 0 5-5m-361 44q0 5 5 5h5m82 0h10m141 0h113q5 0 5-5m-361 29q0 5 5 5h5m50 0h10m84 0h202q5 0 5-5m-361 29q0 5 5 5h5m70 0h30m71 0h20m-106 0q5 0 5 5v8q0 5 5 5h81q5 0 5-5v-8q0-5 5-5m5 0h155q5 0 5-5m-361 44q0 5 5 5h5m62 0h10m124 0h150q5 0 5-5m-361 29q0 5 5 5h5m68 0h10m133 0h135q5 0 5-5m-361 29q0 5 5 5h5m108 0h30m25 0h10m113 0h10m25 0h20m-218 0q5 0 5 5v19q0 5 5 5h5m26 0h162q5 0 5-5v-19q0-5 5-5m5 0h5q5 0 5-5m-361 58q0 5 5 5h5m127 0h10m25 0h10m113 0h10m25 0h26q5 0 5-5m-361 29q0 5 5 5h5m96 0h10m25 0h10m113 0h10m25 0h57q5 0 5-5m-366-359q5 0 5 5v383q0 5 5 5h5m84 0h10m119 0h133q5 0 5-5v-383q0-5 5-5m5 0h5"/><rect class="literal" x="25" y="5" width="69" height="24" rx="7"/><text class="text" x="35" y="21">FORMAT</text><a xlink:href="../grammar_diagrams#format-name"><rect class="rule" x="104" y="5" width="102" height="24"/><text class="text" x="114" y="21">format_name</text></a><rect class="literal" x="25" y="34" width="49" height="24" rx="7"/><text class="text" x="35" y="50">OIDS</text><a xlink:href="../grammar_diagrams#boolean"><rect class="rule" x="104" y="34" width="71" height="24"/><text class="text" x="114" y="50">boolean</text></a><rect class="literal" x="25" y="78" width="68" height="24" rx="7"/><text class="text" x="35" y="94">FREEZE</text><a xlink:href="../grammar_diagrams#boolean"><rect class="rule" x="123" y="78" width="71" height="24"/><text class="text" x="133" y="94">boolean</text></a><rect class="literal" x="25" y="122" width="82" height="24" rx="7"/><text class="text" x="35" y="138">DELIMITER</text><rect class="literal" x="117" y="122" width="141" height="24" rx="7"/><text class="text" x="127" y="138">delimiter_character</text><rect class="literal" x="25" y="151" width="50" height="24" rx="7"/><text class="text" x="35" y="167">NULL</text><rect class="literal" x="85" y="151" width="84" height="24" rx="7"/><text class="text" x="95" y="167">null_string</text><rect class="literal" x="25" y="180" width="70" height="24" rx="7"/><text class="text" x="35" y="196">HEADER</text><a xlink:href="../grammar_diagrams#boolean"><rect class="rule" x="125" y="180" width="71" height="24"/><text class="text" x="135" y="196">boolean</text></a><rect class="literal" x="25" y="224" width="62" height="24" rx="7"/><text class="text" x="35" y="240">QUOTE</text><rect class="literal" x="97" y="224" width="124" height="24" rx="7"/><text class="text" x="107" y="240">quote_character</text><rect class="literal" x="25" y="253" width="68" height="24" rx="7"/><text class="text" x="35" y="269">ESCAPE</text><rect class="literal" x="103" y="253" width="133" height="24" rx="7"/><text class="text" x="113" y="269">escape_character</text><rect class="literal" x="25" y="282" width="108" height="24" rx="7"/><text class="text" x="35" y="298">FORCE_QUOTE</text><rect class="literal" x="163" y="282" width="25" height="24" rx="7"/><text class="text" x="173" y="298">(</text><a xlink:href="../grammar_diagrams#column-names"><rect class="rule" x="198" y="282" width="113" height="24"/><text class="text" x="208" y="298">column_names</text></a><rect class="literal" x="321" y="282" width="25" height="24" rx="7"/><text class="text" x="331" y="298">)</text><rect class="literal" x="163" y="311" width="26" height="24" rx="7"/><text class="text" x="173" y="327">*</text><rect class="literal" x="25" y="340" width="127" height="24" rx="7"/><text class="text" x="35" y="356">FORCE_NOT_NULL</text><rect class="literal" x="162" y="340" width="25" height="24" rx="7"/><text class="text" x="172" y="356">(</text><a xlink:href="../grammar_diagrams#column-names"><rect class="rule" x="197" y="340" width="113" height="24"/><text class="text" x="207" y="356">column_names</text></a><rect class="literal" x="320" y="340" width="25" height="24" rx="7"/><text class="text" x="330" y="356">)</text><rect class="literal" x="25" y="369" width="96" height="24" rx="7"/><text class="text" x="35" y="385">FORCE_NULL</text><rect class="literal" x="131" y="369" width="25" height="24" rx="7"/><text class="text" x="141" y="385">(</text><a xlink:href="../grammar_diagrams#column-names"><rect class="rule" x="166" y="369" width="113" height="24"/><text class="text" x="176" y="385">column_names</text></a><rect class="literal" x="289" y="369" width="25" height="24" rx="7"/><text class="text" x="299" y="385">)</text><rect class="literal" x="25" y="398" width="84" height="24" rx="7"/><text class="text" x="35" y="414">ENCODING</text><rect class="literal" x="119" y="398" width="119" height="24" rx="7"/><text class="text" x="129" y="414">encoding_name</text></svg>

### Grammar
```
copy_from ::= COPY table_name [ ( column_name [, ...] ) ]
                  FROM { filename | PROGRAM command | STDIN }
                  [ [ WITH ] ( option [, ...] ) ] ;

copy_to ::= COPY { table_name [ ( column_name [, ...] ) ] | ( query ) }
                TO ( filename | PROGRAM command | STDOUT )
                [ [ WITH ] ( option [, ...] ) ] ;

copy_option ::=
    FORMAT format_name
    | OIDS [ boolean ]
    | FREEZE [ boolean ]
    | DELIMITER delimiter_character
    | NULL null_string
    | HEADER [ boolean ]
    | QUOTE quote_character
    | ESCAPE escape_character
    | FORCE_QUOTE ( ( column_name [, ...] ) | * )
    | FORCE_NOT_NULL ( column_name [, ...] )
    | FORCE_NULL ( column_name [, ...] )
    | ENCODING encoding_name
```

Where

- `table_name` specifies the table to be copied.

- `column_name` specifies column to be copied.

- `query` can be either SELECT, VALUES, INSERT, UPDATE or DELETE whose results will be copied to files. RETURNING clause must be provided for INSERT, UPDATE and DELETE commands.

- `filename` specifies an absolute or relative path of a file to be copied.

## Examples

- Errors are raised if table does not exist.
- `COPY TO` can only be used with regular tables.
- `COPY FROM` can be used with either tables, foreign tables, or views.

## See Also
[Other YSQL Statements](..)
