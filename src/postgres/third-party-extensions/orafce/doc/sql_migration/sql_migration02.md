Chapter 2	Migrating Syntax Elements
---

This chapter explains how to migrate SQL syntax elements.

### 2.1 Basic Elements

This section explains the basic elements of SQL syntax.

#### 2.1.1 TIMESTAMP Literal

**Description**

A literal with the TIMESTAMP prefix is treated as TIMESTAMP type data.

**Functional differences**

 - **Oracle database**
     - The TIMESTAMP prefix can signify a time zone literal.
 - **PostgreSQL**
     - The TIMESTAMP WITH TIME ZONE prefix signifies a time zone literal. If the prefix is TIMESTAMP only, the time zone value is discarded. No warning, error, or other notification is output.

**Migration procedure**

Use the following procedure to perform migration:

 1. Search for the keyword TIMESTAMP and identify where it is used as a literal prefix.
 2. If a time zone has been specified for the literal, change the prefix to TIMESTAMP WITH TIME ZONE.

**Migration example**

The example below shows how to migrate a TIMESTAMP literal with time zone.


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
<pre><code>INSERT INTO attendance_table 
 VALUES( '1001', 
 'i', 
 <b>TIMESTAMP</b>'2016-05-20 12:30:00 +09:00' );</code></pre>
</td>

<td align="left">
<pre><code>INSERT INTO attendance_table 
 VALUES( '1001',
         'i',
 <b>TIMESTAMP WITH TIME ZONE</b>'2016-05-20 12:30:00 +09:00' );</code></pre>
</td>
</tr>
</tbody>
</table>

#### 2.1.2	Alternate Quotation Literal

**Description**

Using alternate quotation enables a delimiter to be used for a text string.

**Functional differences**

 - **Oracle database**
     - The alternate quotation marks q'x and x' are used. The letter x represents the alternate character.
 - **PostgreSQL**
     - The alternate quotation marks $q$ and $q$ are used.

**Migration procedure**

Use the following procedure to perform migration:

 1. Search for the keyword q' or Q' and identify where alternate quotation marks are used.
 2. Delete alternate characters and single quotation marks where alternate quotation has been used, and enclose strings with $q$. The character between the two $ symbols can be omitted or any string can be specified.

**Migration example**

The example below shows how to migrate alternate quotation.

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
<pre><code>SELECT <b>q'[</b>Adam Electric company's address<b>]'</b> 
 FROM DUAL;</code></pre>
</td>

<td align="left">
<pre><code>SELECT <b>$q$</b>Adam Electric company's address<b>$q$</b> 
 FROM DUAL;</code></pre>
</td>
</tr>
</tbody>
</table>


**See**

----

Refer to "The SQL Language" > "Lexical Structure" > "Dollar-quoted String Constants" in the PostgreSQL Documentation for information on alternate quotation marks.

----

### 2.2 Data Types

This section explains data types.

#### 2.2.1 Migrating Data Types

The table below lists the PostgreSQL data types that correspond to Oracle database data types.

**Data type correspondence**

 - **Character**

|Oracle database Data type	|Remarks	|Migratability	|PostgreSQL Data type	|Remarks	|
|	:---	|	:---	|	:---:	|	:---	|	:---	|
|	CHAR <br> CHARACTER	|	Specifies the number of bytes or number of characters.	|	YR	|	char <br> character	|	Only the number of characters can be specified.	|
|	CLOB <br> CHAR LARGE OBJECT <br> CHARACTER LARGE OBJECT	|		|	MR	|	text <br> Large object	|	Up to 1 GB(text) <br> Up to 4 TB(Large object)	|
|	CHAR VARYING <br> CHARACTER VARYING <br> VARCHAR	|	Specifies the number of bytes or number of characters.	|	YR	|	char varying <br> character varying <br> varchar	|	Only the number of characters can be specified.	<tr><td rowspan="2"> LONG </td><td>  <td align="center"> MR <td> text <td> Up to 1 GB <tr><td> <td align="center"> M <td> Large object <td>
|	NCHAR <br> NATIONAL CHAR <br>NATIONAL CHARACTER	|		|	YR	|	nchar <br>national char <br>national character	|	This data type is internally used as a character type.	|
|	NCHAR VARYING <br>NATIONAL CHAR VARYING <br>NATIONAL CHARACTER VARYING	|		|	YR	|	nchar varying <br>national char varying <br> national character varying	|	This data type is internally used as a character varying type	|
|	NCLOB <br> NCHAR LARGE OBJECT <br> NATIONAL CHARACTER LARGE OBJECT	|		|	MR	|	text <br> Large object	|	Up to 1 GB(text) <br> Up to 4 TB(Large object) <tr><td rowspan="2"> NVARCHAR2 <td> </td> <td align="center">  YR </td> <td> nvarchar2</td> <td> Collating sequence is not supported. <br> This data type is added using orafce. </td><tr> <td></td> <td align="center"> MR </td>  <td> nchar varying <br>national char varying <br> national character varying </td> <td> This data type is internally used as a character varying type. </td><tr><td rowspan="2"> VARCHAR2 <td> Specifies the number of bytes or number of characters. </td> <td align="center"> YR </td> <td> varchar2</td> <td> Only the number of bytes can be specified. <br> Collating sequence is not supported. <br> This data type is added using orafce. </td><tr> <td></td> <td align="center"> MR </td><td> varchar </td> <td> Only the number of characters can be specified. </td>

 - **Numeric**

|Oracle database Data type	|Remarks	|Migratability	|PostgreSQL Data type	|Remarks	|
|	:---	|	:---	|	:---:	|	:---	|	:---	|
|	BINARY_DOUBLE	|		|	M	|	double precision	|		|
|	BINARY_FLOAT	|		|	M	|	real	|		|
|	DOUBLE PRECISION	|		|	Y	|	double precision	|		|
|	FLOAT	|		|	Y	|	float	|		|
|	INT<br>INTEGER	|		|	Y	|	int<br>integer	|		|
|	NUMBER <br> DEC <br> DECIMAL <br> NUMERIC	|	Specifies numbers rounded according to the scale definition.	|	MR	|	smallint <br> integer <br> bigint <br> numeric	|	Integers from -32,768 to +32,767 (smallint) <br> Integers from -2,147,483,648 to +2,147,483,647 (integer)<br> Integers from -9,223,372,036,854,775,808 to +9,223,372,036,854,775,807(bigint)	|
|	REAL	|		|	Y	|	real	|		|
|	SMALLINT	|		|	Y	|	smallint	|		|

 - **Date and time**

|Oracle database Data type	|Remarks	|Migratability	|PostgreSQL Data type	|Remarks	|
|	:---	|	:---	|	:---:	|	:---	|	:---	|
|	INTERVAL DAY TO SECOND	|		|	Y	|	interval day to second	|		|
|	INTERVAL YEAR TO MONTH	|		|	Y	|	interval year to month	|		|
|	TIMESTAMP	|		|	Y	|	timestamp	|		|
|	TIMESTAMP WITH LOCAL TIME ZONE	|		|	M	|	timestamp with time zone	|		|
|	TIMESTAMP WITH TIME ZONE	|		|	Y	|	timestamp with time zone	|	<tr><td rowspan="3"> DATE  <td> </td> <td align="center">  Y </td>  <td> date (orafce)</td> <td> 	The time can be stored in addition to the date. <br> The search_path parameter must be specified.</td><tr> <td> </td>  <td align="center" > YR </td> <td> date (PostgreSQL) </td> <td> Only the date is stored.	<tr><td> </td> <td align="center"> M </td> <td> timestamp </td> <td> 

 - **Binary**

|Oracle database Data type	|Remarks	|Migratability	|PostgreSQL Data type	|Remarks	|
|	:---	|	:---	|	:---:	|	:---	|	:---	|
|	BFILE	|		|	MR	|	bytea <br> Large object	|	Up to 1 GB (bytea) <br>  Up to 4 TB(Large object)	|
|	BLOB <br> BINARY LARGE OBJECT	|		|	MR	|	bytea <br> Large object	|	Up to 1 GB (bytea) <br>  Up to 4 TB(Large object)	|
|	LONG RAW	|		|	MR	|	bytea <br> Large object	|	Up to 1 GB (bytea) <br>  Up to 4 TB(Large object)	|
|	RAW	|		|	M	|	bytea	|		|
|	ROWID	|		|	M	|	oid	|
|	UROWID	|		|	N	|		|		|

 - **Other**

|Oracle database Data type	|Remarks	|Migratability	|PostgreSQL Data type	|Remarks	|
|	:---	|	:---	|	:---:	|	:---	|	:---	|
|	ANYDATA	|		|	N	|		|		|
|	ANYDATASET	|		|	N	|		|		|
|	ANYTYPE	|		|	N	|		|		|
|	DBUriType	|		|	N	|		|		|
|	HTTPUriType	|		|	N	|		|		|
|	MLSLABEL	|		|	N	|		|		|
|	ORDAudio	|		|	N	|		|		|
|	ORDDicom	|		|	N	|		|		|
|	ORDDoc	|		|	N	|		|		|
|	ORDImage	|		|	N	|		|		|
|	ORDVideo	|		|	N	|		|		|
|	REF data type	|		|	N	|		|		|
|	SDO_GEOMETRY	|		|	N	|		|		|
|	SDO_GEORASTER	|		|	N	|		|		|
|	SDO_TOPO_GEOMETRY	|		|	N	|		|		|
|	SI_AverageColor	|		|	N	|		|		|
|	SI_Color	|		|	N	|		|		|
|	SI_ColorHistogram	|		|	N	|		|		|
|	SI_FeatureList	|		|	N	|		|		|
|	SI_PositionalColor	|		|	N	|		|		|
|	SI_StillImage	|		|	N	|		|		|
|	SI_Texture	|		|	N	|		|		|
|	URIFactory package	|		|	N	|		|		|
|	URIType	|		|	N	|		|		|
|	VARRAY	|		|	M	|	Array type	|		|
|	XDBUriType	|		|	N	|		|		|
|	XMLType	|		|	M	|	XML type	|		|
|	Object type	|		|	N	|		|		|
|	Nested table	|		|	N	|		|		|

Y: Data type can be migrated as is

M: Modified data type can be migrated

N: Cannot be migrated

YR: Data type can be migrated as is with restrictions

MR: Modified data type can be migrated with restrictions

#### 2.2.2	Examples of Migrating Data Types

##### 2.2.2.1	Examples of Migrating General Data Types

**Description of migration**

Refer to "Data type correspondence" and change Oracle database data types to PostgreSQL data types.

**Migration example**

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
<pre><code>CREATE TABLE t1( col1 SMALLINT, 
                 col2 <b>VARCHAR2</b>(10), 
                 col3 <b>NVARCHAR2</b>(10), 
                 col4 <b>DATE</b>, 
                 col5 <b>NUMBER</b>(10), 
                 col6 <b>RAW</b>(2000), 
                 col7 <b>BLOB</b> ); </code></pre>
</td>

<td align="left">
<pre><code>CREATE TABLE t1( col1 SMALLINT, 
                 col2 <b>VARCHAR</b>(10), 
                 col3 <b>NCHAR VARYING</b>(10), 
                 col4 <b>TIMESTAMP</b>, 
                 col5 <b>INTEGER</b>, 
                 col6 <b>BYTEA</b>, 
                 col7 <b>BYTEA</b> );
</code></pre>
</td>
</tr>
</tbody>
</table>

##### 2.2.2.2 NUMBER Type

**Functional differences**

 - **Oracle database**
     - A negative value can be specified in a NUMBER type scale. <br>Any value that is specified in a scale beyond the number of significant digits is rounded off.
 - **PostgreSQL**
     - A negative value cannot be specified in a NUMERIC type scale. <br> Any value that is specified in a scale beyond the number of significant digits is discarded.

**Migration procedure**

Use the following procedure to perform migration:

 1. Change DECIMAL scales to 0, and add the number of changed digits to the precision.
 2. Create a function that uses the ROUND function to round off the column that was changed in Step (1) above.
 3. Create a trigger that executes the function created in Step (2) above when the INSERT statement and UPDATE statement are executed.


**Migration example**

The example below shows how to migrate the NUMBER type.

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
<pre><code>CREATE TABLE t1( col1 SMALLINT, 
                 col2 <b>NUMBER(10,-2)</b> ); 
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
 INSERT INTO t1 VALUES( 11, 1234567890 ); 
 SELECT * FROM t1;</code></pre>
</td>

<td align="left">
<pre><code> CREATE TABLE t1( col1 SMALLINT, 
                  col2 <b>NUMERIC(12,0)</b> ); 
<br>
 <b>CREATE FUNCTION f1() RETURNS TRIGGER AS $$ 
 BEGIN 
  NEW.col2 := ROUND(NEW.col2,-2); 
  RETURN NEW; 
 END; 
 $$ LANGUAGE plpgsql; 
 CREATE TRIGGER g1 BEFORE INSERT OR UPDATE ON t1 
 FOR EACH ROW 
 EXECUTE PROCEDURE f1();</b> 
<br>
 INSERT INTO t1 VALUES( 11, 1234567890 ); 
 SELECT * FROM t1;
</code></pre>
</td>
</tr>
</tbody>
</table>



### 2.3 Pseudocolumns

This section explains pseudocolumns.

#### 2.3.1 CURRVAL

**Description**

CURRVAL returns the value nearest to that obtained by NEXTVAL from the sequence in the current session.

**Functional differences**

 - **Oracle database**
     - The sequence name is specified as sequenceName.CURRVAL.
 - **PostgreSQL**
     - The sequence name is specified as CURRVAL('sequenceName').

**Migration procedure**

Use the following procedure to perform migration:

 1. Search for the keyword CURRVAL and identify where it is used.
 2. Change the sequence name specification by placing it after CURRVAL and enclosing it in parentheses and single quotation marks.

**Migration example**

The example below shows how to migrate CURRVAL.

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
<pre><code>SELECT <b>seq1.CURRVAL</b> FROM DUAL;</code></pre>
</td>

<td align="left">
<pre><code>SELECT <b>CURRVAL('seq1')</b> FROM DUAL;
</code></pre>
</td>
</tr>
</tbody>
</table>


#### 2.3.2 NEXTVAL

**Description**

NEXTVAL returns the next number in the sequence.

**Functional differences**

 - **Oracle database**
     - The sequence name is specified as sequenceName.NEXTVAL.
 - **PostgreSQL**
     - The sequence name is specified as NEXTVAL('sequenceName').

**Migration procedure**

Use the following procedure to perform migration:

 1. Search for the keyword NEXTVAL and identify where it is used.
 2. Change the sequence name specification by placing it after NEXTVAL and enclosing it in parentheses and single quotation marks.

**Migration example**

The example below shows how to migrate NEXTVAL.

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
<pre><code>SELECT <b>seq1.NEXTVAL</b> FROM DUAL;</code></pre>
</td>

<td align="left">
<pre><code>SELECT <b>NEXTVAL('seq1')</b> FROM DUAL;
</code></pre>
</td>
</tr>
</tbody>
</table>


#### 2.3.3	ROWID

**Description**

ROWID obtains information for uniquely identifying data.

**Functional differences**

 - **Oracle database**
     - ROWID is created automatically when a table is created.
 - **PostgreSQL**
     - ROWID cannot be used. Use OID instead. However, WITH OIDS must be specified when a table is created.

**Migration procedure**

Use the following procedure to perform migration:

 1. Specify WITH OIDS at the end of the CREATE TABLE statement.
 2. Change the ROWID extraction item in the SELECT statement to OID.

**Migration example**

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
<pre><code> CREATE TABLE t1( col1 INTEGER ); 
<br>
 INSERT INTO t1 VALUES( 11 ); 
 SELECT <b>ROWID</b>, col1 FROM t1;</code></pre>
</td>

<td align="left">
<pre><code> CREATE TABLE t1( col1 INTEGER ) <b>WITH OIDS</b>; 
<br>
 INSERT INTO t1 VALUES( 11 ); 
 SELECT <b>OID</b>, col1 FROM t1;
</code></pre>
</td>
</tr>
</tbody>
</table>


#### 2.3.4 ROWNUM

**Description**

ROWNUM obtains the number of the current row.

##### 2.3.4.1	Obtaining the Row Number

**Functional differences**

 - **Oracle database**
     - ROWNUM obtains the number of the current row.
 - **PostgreSQL**
     - ROWNUM cannot be used. Use ROW_NUMBER() OVER() instead.

**Migration procedure**

Using the ROW_NUMBER() function instead of ROWNUM, perform migration so that the current number is obtained. Use the following procedure to perform migration:

 1. Search for the keyword ROWNUM and identify where it is used.
 2. Change ROWNUM to ROW_NUMBER() OVER().

**Migration example**

The example below shows migration when a line number is obtained.

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
<pre><code>SELECT <b>ROWNUM</b>, i_number, i_name 
 FROM inventory_table;</code></pre>
</td>

<td align="left">
<pre><code>SELECT <b>ROW_NUMBER() OVER()</b>, i_number, i_name 
 FROM inventory_table; </code></pre>
</td>
</tr>
</tbody>
</table>


**Note**

----

This migration example cannot be used with the UPDATE statement.

----

##### 2.3.4.2 Sorting Records and Obtaining the First N Records

**Functional differences**

 - **Oracle database**
     - If a subquery that contains an ORDER BY clause is specified in the FROM clause and a ROWNUM condition is defined in the WHERE clause, the records are sorted and then the first N records are obtained.
 - **PostgreSQL**
     - ROWNUM cannot be used. Using the LIMIT clause instead, sort the records and obtain the first N records.

**Migration procedure**

Use the following procedure to perform migration:

 1. Search for the keyword ROWNUM and identify where it is used.
 2. If an ORDER BY clause is specified in a subquery of the FROM clause and the results are filtered according to the ROWNUM condition in the WHERE clause, regard this portion as an SQL statement that sorts the records and then obtains the first N records.
 3. Move the table name and ORDER BY clause from the FROM clause subquery to a higher SELECT statement and delete the subquery.
 4. In the LIMIT clause, set the same number as the ROWNUM condition of the WHERE clause, and delete the ROWNUM condition from the WHERE clause.

**Migration example**

The example below shows migration when records are sorted and then the first N records are obtained.

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
<pre><code>SELECT i_number, i_name 
 FROM <b>( SELECT * FROM inventory_table 
 ORDER BY i_number DESC )</b> 
 WHERE ROWNUM < 5;</code></pre>
</td>

<td align="left">
<pre><code>SELECT i_number, i_name  
 FROM <b>inventory_table 
 ORDER BY i_number DESC LIMIT 4</b>; 
</code></pre>
</td>
</tr>
</tbody>
</table>


### 2.4 Treatment of NULL and Zero-Length Strings

This section explains how NULL and zero-length strings are treated.

Oracle databases treat zero-length strings as NULL. In contrast, PostgreSQL treats zero-length strings and NULL as two different values.

The table below lists the advantages and disadvantages of using zero-length strings and NULL when performing migration.


**Advantages and disadvantages of retaining or migrating Oracle database zero-length strings**

|Oracle database zero-length strings	|Advantages	|Disadvantages	|
|:---|:---|:---|
|Treated as zero-length strings <br>without being migrated to NULL	|	String concatenation (&#124;&#124;) can be used as is.	|	The target data has fewer hits than with IS NULL. <br>Conditional expressions must be changed.	|
|	Migrated to NULL	|	IS NULL can be used as is.	|	The result of string concatenation (&#124;&#124;) is NULL. <br>String concatenation must be changed.	|

The following sections explain how to make changes if zero-length strings and NULL values are used together.


#### 2.4.1 Search Conditions (IS NULL Predicate)

**Functional differences**

 - **Oracle database**
     - Even zero-length strings hit the IS NULL condition.
 - **PostgreSQL**
     - Zero-length strings do not hit the IS NULL condition.

**Migration procedure**

Use the following procedure to perform migration:

 1. Search for the keyword IS NULL and identify where a NULL search is used.
 2. Change the portions found by the IS NULL search to IS NULL OR strName = ''.

**Migration example**

The example below shows migration when a search for zero-length strings and NULL values is performed.

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
<pre><code>SELECT * FROM staff_table 
 WHERE <b>job IS NULL</b>;</code></pre>
</td>

<td align="left">
<pre><code>SELECT * FROM staff_table 
 WHERE <b>job IS NULL OR job = ''</b>;</code></pre>
</td>
</tr>
</tbody>
</table>


The example below shows migration when a search for values other than zero-length strings and NULL values is performed.

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
<pre><code>SELECT * FROM staff_table 
 WHERE <b>job IS NOT NULL</b>;</code></pre>
</td>

<td align="left">
<pre><code>SELECT * FROM staff_table 
 WHERE <b>job IS NOT NULL AND job != ''</b>;
</code></pre>
</td>
</tr>
</tbody>
</table>



#### 2.4.2 Search Conditions (Comparison Predicate)

**Functional differences**

 - **Oracle database**
     - Zero-length strings are treated as NULL, so they do not match search conditions.
 - **PostgreSQL**
     - Zero-length strings are not treated as NULL, so they can match search conditions.

**Migration procedure**

Use the following procedure to perform migration:

 1. Search for the name of the column where the zero-length string is stored, and identify where a string comparison is used.
 2. Add AND columnName != '' to the search condition.

**Migration example**

The example below shows migration when a zero-length string comparison is specified as the search condition.

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
<pre><code>SELECT * FROM staff_table 
 WHERE job < 'A00';</code></pre>
</td>

<td align="left">
<pre><code>SELECT * FROM staff_table 
 WHERE job < 'A00' <b>AND job != ''</b>;
</code></pre>
</td>
</tr>
</tbody>
</table>


#### 2.4.3 String Concatenation (||)

**Functional differences**

 - **Oracle database**
     - Concatenation with NULL returns strings other than NULL.
 - **PostgreSQL**
     - Concatenation with NULL returns NULL.

**Migration procedure**

Use the following procedure to perform migration:

 1. Search for the keyword || and identify where string concatenation is used.
 2. If the values to be concatenated are likely to become NULL, use the NVL function to return a zero-length string instead of NULL.

**Migration example**

The example below shows migration when NULL is returned by string concatenation (||) in Oracle databases.

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
<pre><code>SELECT 'NAME:' &#124;&#124; <b>name</b> 
 FROM staff_table;</code></pre>
</td>

<td align="left">
<pre><code> SELECT 'NAME:' &#124;&#124; <b>NVL( name, '' )</b> 
  FROM staff_table;
</code></pre>
</td>
</tr>
</tbody>
</table>


