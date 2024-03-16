Chapter 3 Migrating Functions
---

This chapter explains how to migrate SQL functions.

### 3.1 CONVERT

**Description**

CONVERT converts a string from one character set to another.

**Functional differences**

 - **Oracle database**
    - The string is converted from the character set identified in the third argument to the character set identified in the second argument.
 - **PostgreSQL**
     - The string is converted from the character set identified in the second argument to the character set identified in the third argument.

**Migration procedure**

Use the following procedure to perform migration:

 1. Search for the keyword CONVERT and identify where it is used.
 2. Switch the second and third arguments.
 3. Change the character sets in the second and third arguments to names that are valid under the PostgreSQL encoding system.

**Migration example**

The example below shows migration when strings are changed to the character set of the target database.

<table>
<thead>
<tr>
<th align="center">Oracle database</th>
<th align="center">PostgreSQL</th>
</tr>
</thead>
<tbody>
<tr>
<td align="left">
<pre><code>SELECT <b>CONVERT( 'abc', 
 'JA16EUC', 
 'AL32UTF8' )</b> 
 FROM DUAL;</code></pre>
</td>

<td align="left">
<pre><code>SELECT <b>CONVERT( CAST( 'abc' AS BYTEA ), 
  'UTF8', 
 'EUC_JP' )</b> 
 FROM DUAL;</code></pre>
</td>
</tr>
</tbody>
</table>

### 3.2 EMPTY_BLOB

**Description**

EMPTY_BLOB initializes BLOB type areas and creates empty data.

**Functional differences**

 - **Oracle database**
     - BLOB type areas are initialized and empty data is created.
 - **PostgreSQL**
     - EMPTY_BLOB cannot be used. Instead, use a zero-length string for initialization.

**Migration procedure**

Use the following procedure to perform migration:

 1. Search for the keyword EMPTY_BLOB() and identify where it is used.
 2. Change EMPTY_BLOB() to the zero-length string ''.

**Migration example**

The example below shows migration when empty data is inserted into the BLOB column.

<table>
<thead>
<tr>
<th align="center">Oracle database</th>
<th align="center">PostgreSQL</th>
</tr>
</thead>
<tbody>
<tr>
<td align="left">
<pre><code>CREATE TABLE t1( col1 INTEGER, 
 col2 <b>BLOB</b> ); 
<br>
 INSERT INTO t1 VALUES( 11, <b>EMPTY_BLOB()</b> );</code></pre>
</td>

<td align="left">
<pre><code>CREATE TABLE t1( col1 INTEGER, 
 col2 <b>BYTEA</b> ); 
<br>
 INSERT INTO t1 VALUES( 11, '' );</code></pre>
</td>
</tr>
</tbody>
</table>


The example below shows migration when BLOB column data is updated to empty.

<table>
<thead>
<tr>
<th align="center">Oracle database</th>
<th align="center">PostgreSQL</th>
</tr>
</thead>
<tbody>
<tr>
<td align="left">
<pre><code>UPDATE t1 SET col2 = <b>EMPTY_BLOB()</b> WHERE col1 = 11;</code></pre>
</td>

<td align="left">
<pre><code>UPDATE t1 SET col2 = '' WHERE col1 = 11;</code></pre>
</td>
</tr>
</tbody>
</table>

### 3.3 LEAD

**Description**

LEAD obtains the value of the column specified in the arguments from the record that is the specified number of lines below.

**Functional differences**

 - **Oracle database**
     - A NULL value in the column specified in the arguments can be excluded from the calculation.
 - **PostgreSQL**
     - A NULL value in the column specified in the arguments cannot be excluded from the calculation.

**Migration procedure**

Use the following procedure to perform migration:

 1. Search for the keyword LEAD and identify where it is used.
 2. If the IGNORE NULLS clause is specified, check the following values to create a subquery that excludes NULL values:
     - Arguments of LEAD (before IGNORE NULLS)
     - Tables targeted by IGNORE NULLS
     - Columns targeted by IGNORE NULLS
     - Columns to be sorted
 3. Change the table in the FROM clause to a subquery to match the format shown below.
 4. Replace LEAD in the select list with MAX. Specify LEAD_IGNLS in the arguments of MAX, and PARTITION BY CNT in the OVER clause.

~~~
FROM ( SELECT columnBeingUsed,
              CASE
               WHEN ignoreNullsTargetColumn IS NOT NULL THEN
                LEAD( leadFunctionArguments )
                 OVER( PARTITION BY NVL2( ignoreNullsTargetColumn, '0', '1' )
                        ORDER BY sortTargetColumn )
              END AS LEAD_IGNLS,
              COUNT( ignoreNullsTargetColumn )
               OVER( ORDER BY sortTargetColumn
                      ROWS BETWEEN 1 FOLLOWING AND UNBOUNDED FOLLOWING )
               AS CNT
        FROM ignoreNullsTargetTable ) AS T1;
~~~

**Migration example**

The example below shows migration when NULL values are not included in the calculation of column values.

<table>
<thead>
<tr>
<th align="center">Oracle database</th>
<th align="center">PostgreSQL</th>
</tr>
</thead>
<tbody>
<tr>
<td align="left">
<pre><code>SELECT staff_id, 
       name, 
       job, 
 <b>LEAD( job, 1 ) IGNORE NULLS 
 OVER( ORDER BY staff_id DESC) 
 AS "LEAD_IGNORE_NULLS"</b> 
 FROM <b>staff_table 
 ORDER BY staff_id DESC</b>;
<br>
<br>
<br>
<br>
<br>
<br>
<br>
<br>
<br>
<br>
<br>
<br>
<br>
<br>
<br>
<br>
<br>
</code></pre>
</td>

<td align="left">
<pre><code>SELECT staff_id, 
       name, 
       job, 
 <b>MAX( LEAD_IGNLS ) 
 OVER( PARTITION BY CNT ) 
 AS "LEAD_IGNORE_NULLS"</b> 
 FROM <b>( SELECT staff_id, 
       name, 
       job, 
 CASE 
 WHEN job IS NOT NULL THEN 
 LEAD( job, 1 ) 
 OVER( PARTITION BY NVL2( 
       job, 
       '0', 
       '1' ) 
 ORDER BY staff_id DESC) 
 END AS LEAD_IGNLS, 
 COUNT( job ) 
 OVER( ORDER BY staff_id DESC 
  ROWS BETWEEN 1 FOLLOWING 
  AND UNBOUNDED FOLLOWING ) 
 AS CNT 
 FROM staff_table ) AS T1 
 ORDER BY staff_id DESC</b>;</code></pre>
</td>
</tr>
</tbody>
</table>

**Information**

----

If the IGNORE NULLS clause is not specified or if the RESPECT NULLS clause is specified, NULL is included in the calculation, so operation is the same as the LEAD function of PostgreSQL. Therefore, if the IGNORE NULLS clause is not specified, no changes need to be made. If the RESPECT NULLS clause is specified, delete the RESPECT NULLS clause.

The example below shows migration when RESPECT NULLS is specified in the Oracle database.

**LEAD migration example (when RESPECT NULLS is specified)**

<table>
<thead>
<tr>
<th align="center">Oracle database</th>
<th align="center">PostgreSQL</th>
</tr>
</thead>
<tbody>
<tr>
<td align="left">
<pre><code>SELECT staff_id, 
       name, 
       job, 
 LEAD( job, 1 ) 
 <b>RESPECT NULLS</b> 
 OVER( ORDER BY staff_id DESC ) 
 AS "LEAD_RESPECT_NULLS" 
 FROM staff_table 
 ORDER BY staff_id DESC;</code></pre>
</td>

<td align="left">
<pre><code>SELECT staff_id, 
       name, 
       job, 
 LEAD( job, 1 ) 
 OVER( ORDER BY staff_id DESC ) 
 AS "LEAD_RESPECT_NULLS" 
 FROM staff_table 
 ORDER BY staff_id DESC; 
 </code></pre>
</td>
</tr>
</tbody>
</table>

----

### 3.4 RAWTOHEX

**Description**

RAWTOHEX converts RAW type data to a hexadecimal string value.

**Functional differences**

 - **Oracle database**
     - RAW type data is converted to a hexadecimal string value.
 - **PostgreSQL**
     - RAWTOHEX cannot be used. Instead, use ENCODE to convert binary data types corresponding to the RAW type.

**Migration procedure**

Use the following procedure to perform migration:

 1. Search for the keyword RAWTOHEX and identify where it is used.
 2. Change RAWTOHEX to ENCODE and specify HEX in the second argument.

**Migration example**

The example below shows migration when RAW data types are converted to hexadecimal string values.

<table>
<thead>
<tr>
<th align="center">Oracle database</th>
<th align="center">PostgreSQL</th>
</tr>
</thead>
<tbody>
<tr>
<td align="left">
<pre><code>SELECT <b>RAWTOHEX</b> ( 'ABC' ) FROM DUAL;</code></pre>
</td>

<td align="left">
<pre><code>SELECT <b>ENCODE</b> ( 'ABC', <b>'HEX'</b> ) FROM DUAL;</code></pre>
</td>
</tr>
</tbody>
</table>

**Information**

----

A RAWTOHEX function that is used in PL/SQL to take a string as an argument must first be converted to a binary data type using DECODE, and then ENCODE must be used to convert the value to a string.

The example below shows migration of RAWTOHEX when it is used in PL/SQL to take a string as an argument.

**ROWTOHEX migration example (when taking a string as an argument in PL/SQL)**

<table>
<thead>
<tr>
<th align="center">Oracle database</th>
<th align="center">PostgreSQL</th>
</tr>
</thead>
<tbody>
<tr>
<td align="left">
<pre><code>SET SERVEROUTPUT ON; 
<br>
 DECLARE 
 HEX_TEXT VARCHAR2( 100 ); 
 BEGIN 
 <b>HEX_TEXT := RAWTOHEX( '414243' );</b> 
<br>
<br>
 DBMS_OUTPUT.PUT_LINE( HEX_TEXT ); 
 END; 
 / 
 </code></pre>
</td>

<td align="left">
<pre><code> 
 DO $$ 
 DECLARE 
 HEX_TEXT TEXT; 
 BEGIN 
 <b>HEX_TEXT := 
 ENCODE( DECODE( '414243', 'HEX' ), 'HEX' );</b> 
 PERFORM DBMS_OUTPUT.SERVEROUTPUT( TRUE ); 
 PERFORM DBMS_OUTPUT.PUT_LINE( HEX_TEXT ); 
 END; 
 $$ LANGUAGE plpgsql;</code></pre>
</td>
</tr>
</tbody>
</table>

----

### 3.5 REGEXP_REPLACE

**Description**

REGEXP_REPLACE uses a regular expression pattern to replace a string.

**Functional differences**

 - **Oracle database**
     - All strings that match the regular expression pattern are replaced.
 - **PostgreSQL**
     - The first string that matches the regular expression pattern is replaced.

**Migration procedure**

The REGEXP_REPLACE function of PostgreSQL can return the same result if the option string is specified in the fourth argument. Use the following procedure to perform migration:
 
1. Search for the keyword REGEXP_REPLACE and identify where it is used.
 2. Specify the argument 'g' in the fourth argument of REGEXP_REPLACE.

**Migration example**

The example below shows migration when a regular expression pattern is used to convert a string.

<table>
<thead>
<tr>
<th align="center">Oracle database</th>
<th align="center">PostgreSQL</th>
</tr>
</thead>
<tbody>
<tr>
<td align="left">
<pre><code>SELECT REGEXP_REPLACE( '2016', 
       '[0-2]', 
       '*' ) AS "REGEXP_REPLACE" 
 FROM DUAL; 
 </code></pre>
</td>

<td align="left">
<pre><code>SELECT REGEXP_REPLACE( '2016', 
       '[0-2]', 
       '*', 
       <b>'g'</b> ) AS "REGEXP_REPLACE" 
 FROM DUAL;</code></pre>
</td>
</tr>
</tbody>
</table>

### 3.6 TO_TIMESTAMP

**Description**

TO_TIMESTAMP converts a string value to the TIMESTAMP data type.

**Functional differences**

 - **Oracle database**
     - The language to be used for returning the month and day of the week can be specified.
 - **PostgreSQL**
     - The language to be used for returning the month and day of the week cannot be specified.

**Migration procedure**

Use the following procedure to perform migration:

 1. Search for the keyword TO_TIMESTAMP and identify where it is used.
 2. If the third argument of TO_TIMESTAMP is specified, delete it.
 3. If the a string in the first argument contains a national character string, it is replaced with a datetime keyword supported by PostgreSQL.

**Migration example**

The example below shows migration when a string value is converted to the TIMESTAMP data type. One string specifies the month in Japanese as a national character, so it is replaced with the date keyword 'JULY'.

<table>
<thead>
<tr>
<th align="center">Oracle database</th>
<th align="center">PostgreSQL</th>
</tr>
</thead>
<tbody>
<tr>
<td align="left">
<pre><code>SELECT TO_TIMESTAMP('2016/**/21 14:15:30', 
        'YYYY/MONTH/DD HH24:MI:SS', 
        'NLS_DATE_LANGUAGE = Japanese') 
 FROM DUAL;</code></pre>
</td>

<td align="left">
<pre><code>SELECT TO_TIMESTAMP('2016/JULY/21 14:15:30', 
        'YYYY/MONTH/DD HH24:MI:SS') 
<br>
 FROM DUAL;</code></pre>
</td>
</tr>
</tbody>
</table>

\*\*: The July in Japanese

