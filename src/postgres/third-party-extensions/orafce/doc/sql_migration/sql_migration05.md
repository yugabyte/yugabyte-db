Chapter 5	Migrating PL/SQL
---

This chapter explains how to migrate Oracle database PL/SQL. Note that in this document, PL/SQL refers to the language to be migrated to PostgreSQL PL/pgSQL.

### 5.1 Notes on Migrating from PL/SQL to PL/pgSQL
This section provides notes on migration from PL/SQL to PL/pgSQL.

#### 5.1.1 Transaction Control

PL/pgSQL does not allow transaction control within a process. Terminate a procedure whenever a transaction is terminated in the Oracle database and execute a transaction control statement from the application.

### 5.2 Basic Elements
This section explains how to migrate the basic elements of PL/SQL.

#### 5.2.1 Migrating Data Types
The table below lists the PostgreSQL data types that correspond to data types unique to PL/SQL.

Data type correspondence with PL/SQL

 - **Character**

|Oracle database Data type|Remarks|Migratability|PostgreSQL Data type|Remarks|
|:---|:---|:---:|:---|:---|
|	STRING	|	The number of bytes or number of characters can be specified.	|	MR	|	varchar	|	Only the number of characters can be specified.	|

 - **Numeric**

|Oracle database Data type|Remarks|Migratability|PostgreSQL Data type|Remarks|
|:---|:---|:---:|:---|:---|
|	BINARY_INTEGER	|		|	M	|	integer	|		|
|	NATURAL	|		|	M	|	integer	|		|
|	NATURALN	|	Type with NOT NULL constraints	|	MR	|	integer	|	Set "not null" constraints for variable declarations.	|
|	PLS_INTEGER	|		|	M	|	integer	|		|
|	POSITIVE	|		|	M	|	integer	|		|
|	POSITIVEN	|	Type with NOT NULL constraints	|	MR	|	integer	|	Set "not null" constraints for variable declarations.	|
|	SIGNTYPE	|		|	M	|	smallint	|		|
|	SIMPLE_DOUBLE	|	Type with NOT NULL constraints	|	MR	|	double precision	|	Set "not null" constraints for variable declarations.	|
|	SIMPLE_FLOAT	|	Type with NOT NULL constraints	|	MR	|	real	|	Set "not null" constraints for variable declarations.	|
|	SIMPLE_INTEGER	|	Type with NOT NULL constraints	|	MR	|	integer	|	Set "not null" constraints for variable declarations.	|

 - **Date and time**

|Oracle database Data type|Remarks|Migratability|PostgreSQL Data type|Remarks|
|:---|:---|:---:|:---|:---|
|	DSINTERVAL_UNCONSTRAINED	|		|	N	|		|		|
|	TIME_TZ_UNCONSTRAINED	|		|	N	|		|		|
|	TIME_UNCONSTRAINED	|		|	N	|		|		|
|	TIMESTAMP_LTZ_UNCONSTRAINED	|		|	N	|		|		|
|	TIMESTAMP_TZ_UNCONSTRAINED	|		|	N	|		|		|
|	TIMESTAMP_UNCONSTRAINED	|		|	N	|		|		|
|	YMINTERVAL_UNCONSTRAINED	|		|	N	|		|		|

 - **Other**

|Oracle database Data type|Remarks|Migratability|PostgreSQL Data type|Remarks|
|:---|:---|:---:|:---|:---|
|	BOOLEAN	|		|	Y	|	boolean	|		|
|	RECORD	|		|	M	|	Complex type	|		|
|	REF CURSOR (cursor variable)	|		|	M	|	refcursor type	|		|
|	Subtype with constraints	|		|	N	|		|		|
|	Subtype that uses the base type within the same data type family	|		|	N	|		|		|
|	Unconstrained subtype	|		|	N	|		|		|

Y: Data type can be migrated as is

M: Modified data type can be migrated

N: Cannot be migrated

MR: Modified data type can be migrated with restrictions


**See**

----

Refer to "Data Types" for information on migrating data types other than those unique to PL/SQL.

----


#### 5.2.2 Error-Related Elements
This section explains elements related to PL/SQL errors.

##### 5.2.2.1 Predefined Exceptions

**Description**

A predefined exception is an error defined beforehand in an Oracle database.

**Functional differences**

 - **Oracle database**
     - Predefined exceptions can be used.
 - **PostgreSQL**
     - Predefined exceptions cannot be used. Use PostgreSQL error codes instead.

**Migration procedure**

Use the following procedure to perform migration:

 1. Identify where predefined exceptions are used.
 2. Refer to the table below and replace the values of predefined exceptions with PostgreSQL error codes.

|Predefined exception <br> (Oracle database)|Migratability|Corresponding PostgreSQL error code|
|:---|:---:|:---|
|	ACCESS_INTO_NULL	|	N	|	Not generated	|
|	CASE_NOT_FOUND	|	Y	|	case_not_found	|
|	COLLECTION_IS_NULL	|	N	|	Not generated	|
|	CURSOR_ALREADY_OPEN	|	Y	|	duplicate_cursor	|
|	DUP_VAL_ON_INDEX	|	Y	|	unique_violation	|
|	INVALID_CURSOR	|	Y	|	invalid_cursor_name	|
|	INVALID_NUMBER	|	Y	|	invalid_text_representation	|
|	LOGIN_DENIED	|	Y	|	invalid_authorization_specification <br> invalid_password	|
|	NO_DATA_FOUND	|	Y	|	no_data_found	|
|	NO_DATA_NEEDED	|	N	|	Not generated	|
|	NOT_LOGGED_ON	|	N	|	Not generated	|
|	PROGRAM_ERROR	|	Y	|	internal_error	|
|	ROWTYPE_MISMATCH	|	N	|	Not generated	|
|	SELF_IS_NULL	|	N	|	Not generated	|
|	STORAGE_ERROR	|	Y	|	out_of_memory	|
|	SUBSCRIPT_BEYOND_COUNT	|	N	|	Not generated	|
|	SUBSCRIPT_OUTSIDE_LIMIT	|	N	|	Not generated	|
|	SYS_INVALID_ROWID	|	N	|	Not generated	|
|	TIMEOUT_ON_RESOURCE	|	N	|	Not generated	|
|	TOO_MANY_ROWS	|	Y	|	too_many_rows	|
|	VALUE_ERROR	|	Y	|	null_value_not_allowed <br> invalid_text_representation <br> string_data_right_truncation <br> invalid_parameter_value	|
|	ZERO_DIVIDE	|	Y	|	division_by_zero 	|


Y: Can be migrated

N: Cannot be migrated


**Migration example**

The example below shows how to migrate the VALUE_ERROR exception. Note that OR is used in the migration example to group error codes so that VALUE_ERROR corresponds to multiple PostgreSQL error codes.

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
 DECLARE 
  variety VARCHAR2(20) := 'television'; 
  company VARCHAR2(20) := 'Fullmoon Industry'; 
  name VARCHAR2(30); 
 BEGIN 
<br>
  name := ( variety &#124;&#124; 'from' &#124;&#124; company ); 
 EXCEPTION 
  WHEN <b>VALUE_ERROR</b> THEN 
<br>
<br>
<br>
  DBMS_OUTPUT.PUT_LINE ( 
   'ERR: Category length is out of range.' ); 
 END; 
 / 
 </code></pre>
</td>

<td align="left">
<pre><code>DO $$ 
 DECLARE 
  variety VARCHAR(20) := 'television'; 
  company VARCHAR(20) := 'Fullmoon Industry'; 
 name VARCHAR(30); 
 BEGIN 
  PERFORM DBMS_OUTPUT.SERVEROUTPUT(TRUE); 
  name := ( variety &#124;&#124; 'from' &#124;&#124; company ); 
 EXCEPTION 
  WHEN <b>null_value_not_allowed 
       OR invalid_text_representation 
       OR string_data_right_truncation 
       OR invalid_parameter_value THEN</b> 
  PERFORM DBMS_OUTPUT.PUT_LINE ( 
   'ERR: Category length is out of range.' ); 
 END; 
 $$ 
 ; 
 </code></pre>
</td>
</tr>
</tbody>
</table>

##### 5.2.2.2 SQLCODE

**Description**

SQLCODE returns the error code of an error.

**Functional differences**

 - **Oracle database**
     - SQLCODE can be specified to obtain an error code.
 - **PostgreSQL**
     - SQLCODE cannot be specified to obtain an error code.

**Migration procedure**

Use the following procedure to perform migration:

 1. Search for the keyword SQLCODE and identify where it is used.
 2. Change the portion that calls SQLCODE to SQLSTATE.

**Migration example**

The example below shows migration when the code of an error is displayed.

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
 DECLARE 
  v_i_number SMALLINT := 401; 
  v_i_name VARCHAR2(30) := 'Blu-ray and DVD recorder'; 
  v_i_quantity INTEGER := 10; 
  v_i_warehouse SMALLINT := 2; 
 BEGIN 
<br>
 INSERT INTO inventory_table 
  VALUES ( v_i_number, 
           v_i_name, 
           v_i_quantity, 
           v_i_warehouse ); 
 EXCEPTION 
  WHEN OTHERS THEN 
   DBMS_OUTPUT.PUT_LINE( 
    'ERR:' &#124;&#124; <b>SQLCODE</b> &#124;&#124; 
    ': Failure of INSERT.' ); 
 END; 
 / 
 </code></pre>
</td>

<td align="left">
<pre><code>DO $$ 
 DECLARE 
  v_i_number SMALLINT := 401; 
  v_i_name VARCHAR(30) := 'Blu-ray and DVD recorder'; 
  v_i_quantity INTEGER := 10; 
  v_i_warehouse SMALLINT := 2; 
 BEGIN 
 PERFORM DBMS_OUTPUT.SERVEROUTPUT(TRUE); 
 INSERT INTO inventory_table 
  VALUES ( v_i_number, 
           v_i_name, 
           v_i_quantity, 
           v_i_warehouse ); 
 EXCEPTION 
  WHEN OTHERS THEN 
   PERFORM DBMS_OUTPUT.PUT_LINE( 
    'ERR:' &#124;&#124; <b>SQLSTATE</b> &#124;&#124; 
    ': Failure of INSERT.' ); 
 END; 
 $$ 
 ;</code></pre>
</td>
</tr>
</tbody>
</table>

**Note**

----

Oracle databases and PostgreSQL have different error codes, so the set SQLCODE values and SQLSTATE values are different. Refer to "Appendix A. PostgreSQL Error Codes" in the PostgreSQL Documentation for information on the error codes to be defined in PostgreSQL.

----

##### 5.2.2.3 EXCEPTION Declarations

**Description**

An EXCEPTION declaration defines an error.

**Functional differences**

 - **Oracle database**
     - EXCEPTION declarations can be used to define errors.
 - **PostgreSQL**
     - EXCEPTION declarations cannot be used.

**Migration procedure**

EXCEPTION declarations cannot be used, so specify the error number in a RAISE statement to achieve equivalent operation. Use the following procedure to perform migration:

 1. Search for the keyword EXCEPTION, identify where an EXCEPTION declaration is used, and check the error name.
 2. Search for the keyword RAISE and identify where the error created using the EXCEPTION declaration is used.
 3. Delete the error name from the RAISE statement and instead specify the error code using ERRCODE in a USING clause.
 4. Change the portion of the EXCEPTION clause where the error name is used to capture the error to SQLSTATE 'errCodeSpecifiedInStep3'.
 5. Delete the EXCEPTION declaration.

**Migration example**

The example below shows migration when a user-defined error is generated.

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
 DECLARE 
  v_i_number SMALLINT := 200; 
  v_i_name VARCHAR2(20) := 'television'; 
  v_i_quantity INTEGER := 10; 
  v_i_warehouse SMALLINT := 3; 
  <b>warehouse_num_err EXCEPTION;</b> 
 BEGIN 
<br>
  IF ( v_i_warehouse = 1 ) OR ( v_i_warehouse = 2 ) THEN 
   INSERT INTO inventory_table 
    VALUES ( v_i_number, 
             v_i_name, 
             v_i_quantity, 
             v_i_warehouse ); 
  ELSE 
   <b>RAISE warehouse_num_err;</b> 
  END IF; 
 EXCEPTION 
  WHEN <b>warehouse_num_err</b> THEN 
   DBMS_OUTPUT.PUT_LINE( 
    'ERR: Warehouse number is out of range.' ); 
<br>
 END; 
 /</code></pre>
</td>

<td align="left">
<pre><code>SET SERVEROUTPUT ON; 
 DECLARE 
  v_i_number SMALLINT := 200; 
  v_i_name VARCHAR2(20) := 'television'; 
  v_i_quantity INTEGER := 10; 
  v_i_warehouse SMALLINT := 3; 
<br>
 BEGIN 
<br>
  IF ( v_i_warehouse = 1 ) OR ( v_i_warehouse = 2 ) THEN 
   INSERT INTO inventory_table 
    VALUES ( v_i_number, 
             v_i_name, 
             v_i_quantity, 
             v_i_warehouse ); 
  ELSE 
   <b>RAISE USING ERRCODE = '20001';</b> 
  END IF; 
 EXCEPTION 
  WHEN <b>SQLSTATE '20001'</b> THEN 
   DBMS_OUTPUT.PUT_LINE( 
    'ERR: Warehouse number is out of range.' ); 
<br>
 END; 
 /</code></pre>
</td>
</tr>
</tbody>
</table>

##### 5.2.2.4 PRAGMA EXCEPTION_INIT and RAISE_APPLICATION_ERROR

**Description**

An EXCEPTION_INIT pragma associates a user-defined error name with an Oracle database error code. RAISE_APPLICATION_ERROR uses a user-defined error code and error message to issue an error.

**Functional differences**

 - **Oracle database**
     - EXCEPTION_INIT pragmas and RAISE_APPLICATION_ERROR statements can be used.
 - **PostgreSQL**
     - EXCEPTION_INIT pragmas and RAISE_APPLICATION_ERROR statements cannot be used.

**Migration procedure**

EXCEPTION_INIT pragmas and RAISE_APPLICATION_ERROR statements cannot be used, so specify an error message and error code in a RAISE statement to achieve equivalent operation. Use the following procedure to perform migration:

 1. Search for the keywords EXCEPTION and PRAGMA, and check for an EXCEPTION_INIT pragma and the specified error and error code.
 2. Search for the keyword RAISE_APPLICATION_ERROR and check where an error is used.
 3. Replace the error message and error code called by RAISE_APPLICATION_ERROR with syntax that uses a USING clause in RAISE.
 4. Change the portion of the EXCEPTION clause where the user-defined error name is used to capture the error to SQLSTATE 'errCodeSpecifiedInStep3'. To display the error message and error code in the EXCEPTION clause, use SQLERRM and SQLSTATE.
 5. Delete the EXCEPTION declaration and EXCEPTION INIT pragma.

**Migration example**

The example below shows migration when an EXCEPTION INIT pragma and RAISE APPLICATION ERROR statement are used. 

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
 DECLARE 
  v_i_number SMALLINT := 200; 
  v_i_name VARCHAR2(30) := ' liquid crystal?television'; 
  v_i_quantity INTEGER := 10; 
  v_i_warehouse SMALLINT := 3; 
  <b>invalid_length EXCEPTION; 
  PRAGMA EXCEPTION_INIT ( invalid_length, -20001 );</b> 
 BEGIN 
<br>
  IF ( LENGTH( v_i_name ) <= 20 ) THEN 
   INSERT INTO inventory_table 
   VALUES ( v_i_number, 
            v_i_name, 
            v_i_quantity, 
            v_i_warehouse ); 
  ELSE 
  <b>RAISE_APPLICATION_ERROR( 
    -20001, 'ERR: i_name is invalid length.' );</b> 
  END IF; 
 EXCEPTION 
  WHEN <b>invalid_length</b> THEN 
   <b>DBMS_OUTPUT.PUT_LINE( 
    TO_CHAR(SQLERRM(-20001)) );</b> 
<br>
 END; 
 /</code></pre>
</td>

<td align="left">
<pre><code>DO $$ 
 DECLARE 
  v_i_number SMALLINT := 200; 
  v_i_name VARCHAR(30) := ' liquid crystal television'; 
  v_i_quantity INTEGER := 10; 
  v_i_warehouse SMALLINT := 3; 
<br>
<br>
 BEGIN 
  PERFORM DBMS_OUTPUT.SERVEROUTPUT( TRUE ); 
  IF ( LENGTH( v_i_name ) <= 20 ) THEN 
   INSERT INTO inventory_table 
    VALUES ( v_i_number, 
             v_i_name, 
             v_i_quantity, 
             v_i_warehouse ); 
  ELSE 
   <b>RAISE 'ERR: i_name is invalid length.' 
    USING ERRCODE = '20001';</b> 
  END IF; 
 EXCEPTION 
  WHEN <b>SQLSTATE '20001'</b> THEN 
   <b>PERFORM DBMS_OUTPUT.PUT_LINE( 
    SQLSTATE &#124;&#124; ':' &#124;&#124; SQLERRM );</b> 
 END; 
 $$ 
 ;</code></pre>
</td>
</tr>
</tbody>
</table>

**Note**

----

SQLERRM provided by PostgreSQL cannot specify an error code in its argument.

----

##### 5.2.2.5 WHENEVER

**Description**

WHENEVER SQLERROR predefines the processing to be run when an error occurs in an SQL statement or PL/SQL.
WHENEVER OSERROR predefines the processing to be run when an operating system error occurs.

**Functional differences**

 - **Oracle database**
     - WHENEVER can be used to predefine the processing to be run when an error occurs.
 - **PostgreSQL**
     - WHENEVER cannot be used.

**Migration procedure**

Use the following procedure to perform migration:

 1. Search for the keyword WHENEVER and identify where it is used.
 2. Replace WHENEVER SQLERROR EXIT FAILURE syntax or WHENEVER OSERROR EXIT FAILURE syntax with \set ON_ERROR_STOP ON.

**Migration example**

The example below shows migration when an active script that encounters an error is stopped.

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
<pre><code><b>WHENEVER SQLERROR EXIT FAILURE</b> 
<br>
 DECLARE 
  v_i_number SMALLINT := 401; 
  v_i_name VARCHAR2(30) := 'liquid crystal television'; 
  v_i_quantity INTEGER := 100; 
  v_i_warehouse SMALLINT := 2; 
 BEGIN 
  INSERT INTO inventory_table 
   VALUES ( v_i_number, 
            v_i_name, 
            v_i_quantity, 
            v_i_warehouse ); 
 END; 
 / 
 </code></pre>
</td>

<td align="left">
<pre><code><b>\set ON_ERROR_STOP ON</b> 
 DO $$ 
 DECLARE 
  v_i_number SMALLINT := 401; 
  v_i_name VARCHAR(30) := 'liquid crystal television'; 
  v_i_quantity INTEGER := 100; 
  v_i_warehouse SMALLINT := 2; 
 BEGIN 
  INSERT INTO inventory_table 
   VALUES ( v_i_number, 
            v_i_name, 
            v_i_quantity, 
            v_i_warehouse ); 
 END; 
 $$ 
 ;</code></pre>
</td>
</tr>
</tbody>
</table>


**Note**

----

 - WHENEVER SQLERROR and WHENEVER OSERROR are SQL*Plus features. Migrate them to the psql feature in PostgreSQL.
 - Of the values that can be specified in WHENEVER, only EXIT FAILURE and CONTINUE NONE can be migrated. If CONTINUE NONE is specified, replace it with \set ON_ERROR_ROLLBACK ON.

----

#### 5.2.3 Cursor-Related Elements

This section explains elements related to PL/SQL cursors.

##### 5.2.3.1 %FOUND

**Description**

%FOUND obtains information on whether an SQL statement affected one or more rows.

**Functional differences**

 - **Oracle database**
     - %FOUND can be used.
 - **PostgreSQL**
     - %FOUND cannot be used. Use FOUND instead.

**Migration procedure**

Use the following procedure to perform migration with FOUND:

 - When there is one implicit or explicit cursor
     1. Search for the keyword %FOUND and identify where it is used.
     2. Change the portion that calls cursorName%FOUND to FOUND.
 - When there are multiple explicit cursors
     1. Search for the keyword %FOUND and identify where it is used.
     2. Using DECLARE, declare the same number of BOOLEAN variables as explicit cursors.
     3. Immediately after each FETCH statement, store the value obtained by FOUND in the variable declared in step 2.
     4. Replace the portion that calls cursorName%FOUND with the variable used in step 3.

**Migration example**

The example below shows migration when update of a row by an implicit cursor is checked.

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
 BEGIN 
<br>
  UPDATE inventory_table SET i_warehouse = 3 
   WHERE i_name = 'television'; 
  IF <b>SQL%FOUND</b> THEN 
   DBMS_OUTPUT.PUT_LINE ( 'Updated!' ); 
  ELSE 
   DBMS_OUTPUT.PUT_LINE ( 'Not Updated!' ); 
  END IF; 
 END; 
 / 
 </code></pre>
</td>

<td align="left">
<pre><code>DO $$ 
 BEGIN 
  PERFORM DBMS_OUTPUT.SERVEROUTPUT(TRUE); 
  UPDATE inventory_table SET i_warehouse = 3 
   WHERE i_name = 'television'; 
  IF <b>FOUND</b> THEN 
   PERFORM DBMS_OUTPUT.PUT_LINE( 'Updated!' ); 
  ELSE 
   PERFORM DBMS_OUTPUT.PUT_LINE( 'Not updated!' ); 
  END IF; 
 END; 
 $$ 
 ;</code></pre>
</td>
</tr>
</tbody>
</table>

**Note**

----

Statements in which %FOUND is determined to be NULL cannot be migrated. If SQL has not been executed at all, FOUND is set to FALSE, which is the same return value as when no row has been affected.

----

##### 5.2.3.2 %NOTFOUND

**Description**

%NOTFOUND obtains information on whether an SQL statement affected no rows.

**Functional differences**

 - **Oracle database**
     - %NOTFOUND can be used.
 - **PostgreSQL**
     - %NOTFOUND cannot be used. Use NOT FOUND instead.

**Migration procedure**

Use the following procedure to perform migration:

 - When there is one implicit or explicit cursor
     1. Search for the keyword %NOTFOUND and identify where it is used.
     2. Change the portion that calls cursorName%NOTFOUND to NOT FOUND.
 - When there are multiple explicit cursors
     1. Search for the keyword %NOTFOUND and identify where it is used.
     2. Using DECLARE, declare the same number of BOOLEAN variables as explicit cursors.
     3. Immediately after each FETCH statement, store the value obtained by FOUND in the variable declared in step 2.
     4. Replace the portion that calls cursorName%NOTFOUND with negation of the variable used in step 3.

**Migration example**

The example below shows migration when multiple explicit cursors are used to repeat FETCH until there are no more rows in one of the tables.

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
<pre><code> SET SERVEROUTPUT ON; 
 DECLARE 
  CURSOR cur1 IS 
   SELECT i_number, i_name 
    FROM inventory_table 
    WHERE i_name = 'television'; 
  CURSOR cur2 IS 
   SELECT i_number, i_name 
    FROM inventory_table 
    WHERE i_name = 'cd player'; 
  v1_i_number inventory_table.i_number%TYPE; 
  v2_i_number inventory_table.i_number%TYPE; 
  v1_i_name inventory_table.i_name%TYPE; 
  v2_i_name inventory_table.i_name%TYPE; 
<br>
<br>
 BEGIN 
<br>
  OPEN cur1; 
  OPEN cur2; 
  LOOP 
   FETCH cur1 into v1_i_number, v1_i_name; 
<br>
   FETCH cur2 into v2_i_number, v2_i_name; 
<br>
   EXIT WHEN ( <b>cur1%NOTFOUND</b> ) OR ( <b>cur2%NOTFOUND</b> ); 
   DBMS_OUTPUT.PUT_LINE( 
    'No.' &#124;&#124; v1_i_number &#124;&#124; ': ' &#124;&#124; v1_i_name ); 
   DBMS_OUTPUT.PUT_LINE( 
    'No.' &#124;&#124; v2_i_number &#124;&#124; ': ' &#124;&#124; v2_i_name ); 
  END LOOP; 
  CLOSE cur1; 
  CLOSE cur2; 
 END; 
 / 
 </code></pre>
</td>

<td align="left">
<pre><code>DO $$ 
 DECLARE 
  cur1 CURSOR FOR 
   SELECT i_number, i_name 
    FROM inventory_table 
    WHERE i_name = 'television'; 
  cur2 CURSOR FOR 
   SELECT i_number, i_name 
    FROM inventory_table 
    WHERE i_name = 'cd player'; 
  v1_i_number inventory_table.i_number%TYPE; 
  v2_i_number inventory_table.i_number%TYPE; 
  v1_i_name inventory_table.i_name%TYPE; 
  v2_i_name inventory_table.i_name%TYPE; 
  <b>flg1 BOOLEAN; 
  flg2 BOOLEAN;</b> 
 BEGIN 
  PERFORM DBMS_OUTPUT.SERVEROUTPUT( TRUE ); 
  OPEN cur1; 
  OPEN cur2; 
  LOOP 
   FETCH cur1 into v1_i_number, v1_i_name; 
   <b>flg1 := FOUND;</b> 
   FETCH cur2 into v2_i_number, v2_i_name; 
   <b>flg2 := FOUND;</b> 
   EXIT WHEN ( <b>NOT flg1</b> ) OR ( <b>NOT flg2</b> ); 
   PERFORM DBMS_OUTPUT.PUT_LINE( 
    'No.' &#124;&#124; v1_i_number &#124;&#124; ': ' &#124;&#124; v1_i_name ); 
   PERFORM DBMS_OUTPUT.PUT_LINE( 
    'No.' &#124;&#124; v2_i_number &#124;&#124; ': ' &#124;&#124; v2_i_name ); 
  END LOOP; 
  CLOSE cur1; 
  CLOSE cur2; 
 END; 
 $$ 
 ;</code></pre>
</td>
</tr>
</tbody>
</table>

**Note**

----

Statements in which %NOTFOUND is determined to be NULL cannot be migrated. If SQL has not been executed at all, FOUND is set to FALSE, which is the same return value as when no row has been affected.

----

##### 5.2.3.3 %ROWCOUNT

**Description**

%ROWCOUNT indicates the number of rows processed by an SQL statement.

**Functional differences**

 - **Oracle database**
     - %ROWCOUNT can be used.
 - **PostgreSQL**
     - %ROWCOUNT cannot be used. Use ROW_COUNT instead.

**Migration procedure**

Use the following procedure to perform migration:

 1. Search for the keyword %ROWCOUNT and identify where it is used.
 2. Declare the variable that will store the value obtained by ROW_COUNT.
 3. Use GET DIAGNOSTICS immediately in front of %ROWCOUNT identified in step 1. It obtains ROW_COUNT and stores its value in the variable declared in step 2.
 4. Replace the portion that calls %ROWCOUNT with the variable used in step 3.

**Migration example**

The example below shows migration when the number of deleted rows is obtained.

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
<br>
 BEGIN 
<br>
  DELETE FROM inventory_table 
   WHERE i_name = 'television'; 
<br>
  DBMS_OUTPUT.PUT_LINE ( 
   TO_CHAR( <b>SQL%ROWCOUNT</b> ) &#124;&#124; 'rows deleted!' ); 
 END; 
 / 
 </code></pre>
</td>

<td align="left">
<pre><code>DO $$ 
 <b>DECLARE 
  row_num INTEGER;</b> 
 BEGIN 
  PERFORM DBMS_OUTPUT.SERVEROUTPUT( TRUE ); 
  DELETE FROM inventory_table 
   WHERE i_name = 'television'; 
  <b>GET DIAGNOSTICS row_num := ROW_COUNT;</b> 
  PERFORM DBMS_OUTPUT.PUT_LINE( 
   TO_CHAR( row_num ) &#124;&#124; 'rows deleted!' ); 
 END; 
 $$ 
 ;</code></pre>
</td>
</tr>
</tbody>
</table>

**Note**

Statements in which %ROWCOUNT is determined to be NULL cannot be migrated. If SQL has not been executed at all, 0 is set.

##### 5.2.3.4 REF CURSOR

**Description**

REF CURSOR is a data type that represents the cursor in Oracle databases.

**Functional differences**

 - **Oracle database**
     - REF CURSOR type variables can be defined.
 - **PostgreSQL**
     - REF CURSOR type variables cannot be defined. Use refcursor type variables instead.

**Migration procedure**

Use the following procedure to perform migration:

 1. Search for the keyword REF CURSOR and identify where it is used.
 2. Delete the REF CURSOR type definition and the portion where the cursor variable is declared using that type.
 3. Change the specification so that the cursor variable is declared using the refcursor type.

**Migration example**

The example below shows migration when the cursor variable is used to FETCH a row.

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
 DECLARE 
  <b>TYPE curtype IS REF CURSOR; 
  cur curtype;</b> 
  v_inventory inventory_table%ROWTYPE; 
 BEGIN 
<br>
  OPEN cur FOR 
   SELECT * FROM inventory_table 
    WHERE i_warehouse = 2; 
  DBMS_OUTPUT.PUT_LINE( 'In warehouse no.2 is :' ); 
<br>
  LOOP 
   FETCH cur into v_inventory; 
   EXIT WHEN cur%NOTFOUND; 
   DBMS_OUTPUT.PUT_LINE( 
    'No.' &#124;&#124; v_inventory.i_number &#124;&#124; 
    ': ' &#124;&#124; v_inventory.i_name &#124;&#124; 
    '(' &#124;&#124; v_inventory.i_quantity &#124;&#124; ')' ); 
  END LOOP; 
  CLOSE cur; 
 END; 
 / 
 </code></pre>
</td>

<td align="left">
<pre><code>DO $$ 
 DECLARE 
  <b>cur refcursor;</b> 
<br>
  v_inventory inventory_table%ROWTYPE; 
 BEGIN 
  PERFORM DBMS_OUTPUT.SERVEROUTPUT(TRUE); 
  OPEN cur FOR 
   SELECT * FROM inventory_table 
    WHERE i_warehouse = 2; 
  PERFORM DBMS_OUTPUT.PUT_LINE( 
   'In warehouse no.2 is :' ); 
  LOOP 
   FETCH cur into v_inventory; 
   EXIT WHEN NOT FOUND; 
   PERFORM DBMS_OUTPUT.PUT_LINE( 
    'No.' &#124;&#124; v_inventory.i_number &#124;&#124; 
    ': ' &#124;&#124; v_inventory.i_name &#124;&#124; 
    '(' &#124;&#124; v_inventory.i_quantity &#124;&#124; ')' ); 
  END LOOP; 
  CLOSE cur; 
 END; 
 $$ 
 ;</code></pre>
</td>
</tr>
</tbody>
</table>

**Note**

-----

The RETURN clause (specifies the return type of the cursor itself) cannot be specified in the refcursor type provided by PostgreSQL.

-----

##### 5.2.3.5 FORALL

**Description**

FORALL uses the changing value of the VALUES clause or WHERE clause to execute a single command multiple times.

**Functional differences**

 - **Oracle database**
     - FORALL statements can be used.
 - **PostgreSQL**
     - FORALL statements cannot be used.

**Migration procedure**

FORALL statements cannot be used, so replace them with FOR statements so that the same result is returned. Use the following procedure to perform migration:

 1. Search for the keyword FORALL and identify where it is used.
 2. Store the elements used by commands within FORALL in array type variables. In addition, delete Oracle database array definitions.
 3. Replace FORALL statements with FOR - LOOP statements.
 4. Replace portions that reference an array in the Oracle database with referencing of the array type variable defined in step 2. The portions changed in the migration example and details of the changes are as follows:
     - Start of the loop: Change i_numL.FIRST to 1.
     - End of the loop: Replace i_numL.LAST with ARRAY_LENGTH.
     - Referencing of array elements: Change i_numL(i) to i_numL[i].

**Migration example**

The example below shows migration when FORALL is used to execute INSERT.

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
<pre><code> 
 DECLARE 
   <b>TYPE NumList IS TABLE OF SMALLINT; 
   i_numL NumList := NumList( 151, 
                              152, 
                              153, 
                              154, 
                              155 );</b> 
 BEGIN 
   <b>FORALL</b> i IN <b>i_numL.FIRST .. i_numL.LAST</b> 
     INSERT INTO inventory_table 
     VALUES ( <b>i_numL(i)</b>, 'television', 10, 2 ); 
<br>
 END; 
 / 
 </code></pre>
</td>

<td align="left">
<pre><code>DO $$ 
 DECLARE 
<br>
   <b>i_numL SMALLINT ARRAY := '{ 151, 
                               152, 
                               153, 
                               154, 
                               155 }';</b> 
 BEGIN 
  <b>FOR</b> i IN <b>1..ARRAY_LENGTH( i_numL, 1 ) LOOP</b> 
     INSERT INTO inventory_table 
     VALUES ( <b>i_numL[i]</b>, 'television', 10, 2 ); 
   END LOOP; 
 END; 
 $$ 
 ;</code></pre>
</td>
</tr>
</tbody>
</table>

### 5.3 Migrating Functions

This section explains how to migrate PL/SQL functions.

**Description**

A stored function is a user-defined function that returns a value.

#### 5.3.1 Defining Functions

**Functional differences**

 - **Oracle database**
     - A RETURN clause within a function prototype is specified as RETURN. <br> DECLARE does not need to be specified as the definition portion of a variable used within a function.
 - **PostgreSQL**
     - Use RETURNS to specify a RETURN clause within a function prototype. <br> DECLARE must be specified as the definition portion of a variable to be used within a function.

**Migration procedure**

Use the following procedure to perform migration:

 1. Search for the keywords CREATE and FUNCTION, and identify where user-defined functions are created.
 2. If an IN or OUT qualifier is specified in an argument, move it to the beginning of the parameters.
 3. Change RETURN within the function prototype to RETURNS.
 4. Change the AS clause to AS $$. (If the keyword is IS, change it to AS.)
 5. If a variable is defined, add the DECLARE keyword after $$.
 6. Delete the final slash (/) and specify $$ and a LANGUAGE clause.

**Migration example**

The example below shows migration when CREATE FUNCTION is used to define a function.

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
<pre><code>CREATE FUNCTION PROFIT_FUNC( 
  selling <b>IN</b> INTEGER, 
  sales_num <b>IN</b> INTEGER, 
  cost <b>IN</b> INTEGER 
 ) <b>RETURN</b> INTEGER AS 
<br>
     profit INTEGER; 
   BEGIN 
     profit := ( ( selling * sales_num ) - cost ); 
     RETURN profit; 
   END; 
 /</code></pre>
</td>

<td align="left">
<pre><code>CREATE FUNCTION PROFIT_FUNC( 
  <b>IN</b> selling INTEGER, 
  <b>IN</b> sales_num INTEGER, 
  <b>IN</b> cost INTEGER 
 ) <b>RETURNS</b> INTEGER AS <b>$$ 
 DECLARE</b> 
     profit INTEGER; 
 BEGIN 
     profit := ( ( selling * sales_num ) - cost ); 
     RETURN profit; 
 END; 
 <b>$$ LANGUAGE plpgsql;</b></code></pre>
</td>
</tr>
</tbody>
</table>

### 5.4 Migrating Procedures

This section explains how to migrate PL/SQL procedures. 

**Description**

A stored procedure is a single procedure into which multiple processes have been grouped.

#### 5.4.1 Defining Procedures

**Functional differences**

 - **Oracle database**
     - Procedures can be created.
 - **PostgreSQL**
     - Procedures cannot be created.

**Migration procedure**

Procedures cannot be created in PostgreSQL. Therefore, replace them with functions. Use the following procedure to perform migration:

 1. Search for the keywords CREATE and PROCEDURE, and identify where a procedure is defined.
 2. Replace the CREATE PROCEDURE statement with the CREATE FUNCTION statement.
 3. Change the AS clause to RETURNS VOID AS $$. (If the keyword is IS, change it to AS.)
 4. If a variable is defined, add the DECLARE keyword after $$.
 5. Delete the final slash (/) and specify $$ and a LANGUAGE clause.

**Note**

----

If the OUT or INOUT keywords are specified in the arguments, a different migration method must be used. Refer to "Defining Procedures That Return a Value".

----

**Migration example**

The example below shows migration when a procedure is defined.

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
<pre><code><b>CREATE PROCEDURE</b> UPD_QUANTITY ( 
  upd_number SMALLINT, 
  upd_quantity INTEGER 
 ) <b>AS</b> 
   BEGIN 
     UPDATE inventory_table 
 SET i_quantity = upd_quantity 
       WHERE i_number = upd_number; 
   END; 
 / 
 ------------------------------------------------- 
<br>
 DECLARE 
   v_i_number SMALLINT := 110; 
   v_i_quantity INTEGER := 100; 
 BEGIN 
   upd_quantity( v_i_number, v_i_quantity ); 
 END; 
 / 
 </code></pre>
</td>

<td align="left">
<pre><code><b>CREATE FUNCTION</b> UPD_QUANTITY ( 
  upd_number SMALLINT, 
  upd_quantity INTEGER 
 ) <b>RETURNS VOID AS $$</b> 
 BEGIN 
     UPDATE inventory_table 
 SET i_quantity = upd_quantity 
       WHERE i_number = upd_number; 
 END; 
 <b>$$ LANGUAGE plpgsql;</b> 
 ------------------------------------------------- 
 DO $$ 
 DECLARE 
   v_i_number SMALLINT := 110; 
   v_i_quantity INTEGER := 100; 
 BEGIN 
   <b>PERFORM</b> upd_quantity( v_i_number, v_i_quantity ); 
 END; 
 $$ 
 ;</code></pre>
</td>
</tr>
</tbody>
</table>

#### 5.4.2 Calling Procedures

**Functional differences**

 - **Oracle database**
     - A procedure can be called as a statement.
 - **PostgreSQL**
     - Procedures cannot be used. Instead, call the procedure as a function that does not return a value.

**Migration procedure**

Use the following procedure to perform migration:

 1. Identify where each procedure is called.
 2. Specify PERFORM in front of the procedure call.

**Migration example**

The example below shows migration when a procedure is called.

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
 BEGIN 
<br>
   DBMS_OUTPUT.PUT_LINE( 'Hello World.' ); 
 END; 
 / 
 </code></pre>
</td>

<td align="left">
<pre><code>DO $$ 
 BEGIN 
   PERFORM DBMS_OUTPUT.SERVEROUTPUT( TRUE ); 
   <b>PERFORM</b> DBMS_OUTPUT.PUT_LINE( 'Hello World.' ); 
 END; 
 $$ 
 ;</code></pre>
</td>
</tr>
</tbody>
</table>

#### 5.4.3 Defining Procedures That Return a Value

**Functional differences**

 - **Oracle database**
     - Procedures that return a value can be created.
 - **PostgreSQL**
      - Procedures that return a value cannot be created.

**Migration procedure**

Use the following procedure to perform migration:

 1. Search for the CREATE and PROCEDURE keywords, and identify where a procedure is defined.
 2. Confirm that the OUT or INOUT keyword is specified in the arguments.
 3. Replace the CREATE PROCEDURE statement with the CREATE FUNCTION statement.
 4. If the IN, OUT, or INOUT keyword is specified in the arguments, move it to the beginning of the arguments.
 5. Change the AS clause to AS $$. (If the keyword is IS, change it to AS.)
 6. If a variable is defined, add the DECLARE keyword after $$.
 7. Delete the final slash (/) and specify $$ and a LANGUAGE clause.
 8. If calling a function, call it without specifying arguments in the OUT parameter and store the return value in the variable. If there are multiple OUT parameters, use a SELECT INTO statement.

**Migration example**

The example below shows migration when the OUT parameter of CREATE PROCEDURE is used to define a procedure that returns a value.

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
<pre><code> <b>CREATE PROCEDURE</b> remove_row ( 
  del_name VARCHAR2, 
  <b>del_row OUT</b> INTEGER 
 ) <b>AS</b> 
  BEGIN 
   DELETE FROM inventory_table 
    WHERE i_name = del_name; 
   del_row := SQL%ROWCOUNT; 
  END; 
 / 
 ------------------------------------------------- 
 SET SERVEROUTPUT ON; 
 DECLARE 
  rtn_row INTEGER; 
  v_i_name VARCHAR2(20) := 'television'; 
 BEGIN 
<br>
  <b>remove_row( v_i_name, rtn_row );</b> 
  DBMS_OUTPUT.PUT_LINE( 
   TO_CHAR( rtn_row ) &#124;&#124; 'rows deleted!' ); 
 END; 
 / 
 </code></pre>
</td>

<td align="left">
<pre><code><b>CREATE FUNCTION</b> remove_row ( 
  del_name VARCHAR, 
  <b>OUT del_row</b> INTEGER 
 ) <b>AS $$</b> 
  BEGIN 
   DELETE FROM inventory_table 
    WHERE i_name = del_name; 
   GET DIAGNOSTICS del_row := ROW_COUNT; 
  END; 
 <b>$$ LANGUAGE plpgsql;</b> 
 ------------------------------------------------- 
 DO $$ 
 DECLARE 
  rtn_row INTEGER; 
  v_i_name VARCHAR(20) := 'television'; 
 BEGIN 
  PERFORM DBMS_OUTPUT.SERVEROUTPUT( TRUE ); 
  <b>rtn_row := remove_row( v_i_name );</b> 
  PERFORM DBMS_OUTPUT.PUT_LINE( 
   TO_CHAR( rtn_row ) &#124;&#124; 'rows deleted!' ); 
 END; 
 $$ 
 ;</code></pre>
</td>
</tr>
</tbody>
</table>


**See**

----

Refer to "Defining Nested Procedures" for examples of migrating a call portion that uses a SELECT INTO statement.

----

#### 5.4.4 Defining Nested Procedures

**Functional differences**

 - **Oracle database**
     - Nested procedures can be defined.
 - **PostgreSQL**
     - Nested procedures cannot be defined.

**Migration procedure**

Procedures must be replaced with functions, but functions cannot be nested in PostgreSQL. Therefore, define and call the functions separately. Use the following procedure to perform migration:

 1. Search for the CREATE and PROCEDURE keywords, and identify where a procedure is defined.
 2. If a PROCEDURE statement is defined in a DECLARE clause, regard it as a nested procedure.
 3. Check for variables that are used by both the procedure and the nested procedure.
 4. Replace a nested procedure (from PROCEDURE procedureName to END procedureName;) with a CREATE FUNCTION statement. Specify the variables you found in step 3 in the INOUT argument of CREATE FUNCTION.
 5. Replace the portion that calls the nested procedure with a SELECT INTO statement. Specify the common variables you found in step 3 in both the variables used for calling the function and the variables used for accepting the INTO clause.

**Migration example**

The example below shows migration when nested procedures are used and a variable is shared by a procedure and its call portion.

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
 DECLARE 
  sales_num INTEGER; 
  stock_num INTEGER; 
  v_i_quantity INTEGER; 
  <b>PROCEDURE quantity_check ( 
   sales INTEGER, 
   stock INTEGER 
  ) IS</b> 
   quantity_err EXCEPTION; 
  BEGIN 
<br>
   v_i_quantity := ( stock - sales ); 
   IF ( v_i_quantity < 0 ) THEN 
    RAISE quantity_err; 
   END IF; 
  EXCEPTION 
   WHEN quantity_err THEN 
    DBMS_OUTPUT.PUT_LINE( 
     'ERR: i_quantity is negative value.' ); 
  <b>END quantity_check;</b> 
<br>
<br>
<br>
<br>
 BEGIN 
<br>
  sales_num := 80; 
  stock_num := 100; 
  <b>quantity_check( sales_num, stock_num );</b> 
<br>
  DBMS_OUTPUT.PUT_LINE( 
   'i_quantity: ' &#124;&#124; v_i_quantity ); 
<br>
  sales_num := 100; 
  stock_num := 80; 
  <b>quantity_check( sales_num, stock_num );</b> 
<br>
  DBMS_OUTPUT.PUT_LINE( 
   'i_quantity: ' &#124;&#124; v_i_quantity ); 
 END; 
 / 
<br>
<br>
<br>
<br>
 </code></pre>
</td>

<td align="left">
<pre><code> 
 <b>CREATE FUNCTION quantity_check( 
  sales INTEGER, 
  stock INTEGER, 
  INOUT quantity INTEGER 
 ) AS $$</b> 
<br>
  BEGIN 
   PERFORM DBMS_OUTPUT.SERVEROUTPUT( TRUE ); 
   quantity := ( stock - sales ); 
   IF ( quantity < 0 ) THEN 
    RAISE USING ERRCODE = '20001'; 
   END IF; 
  EXCEPTION 
   WHEN SQLSTATE '20001' THEN 
    PERFORM DBMS_OUTPUT.PUT_LINE( 
     'ERR: i_quantity is negative value.' ); 
  <b>END; 
 $$ LANGUAGE plpgsql;</b> 
 ------------------------------------------------- 
 DO $$ 
 DECLARE 
  sales_num INTEGER; 
  stock_num INTEGER; 
  v_i_quantity INTEGER; 
 BEGIN 
  PERFORM DBMS_OUTPUT.SERVEROUTPUT( TRUE ); 
  sales_num := 80; 
  stock_num := 100; 
  <b>SELECT quantity INTO v_i_quantity 
   FROM quantity_check( sales_num, 
                        stock_num, 
                        v_i_quantity );</b> 
  PERFORM DBMS_OUTPUT.PUT_LINE( 
   'i_quantity: ' &#124;&#124; v_i_quantity ); 
<br>
  sales_num := 100; 
  stock_num := 80; 
  <b>SELECT quantity INTO v_i_quantity 
   FROM quantity_check( sales_num, 
                        stock_num, 
                        v_i_quantity );</b> 
  PERFORM DBMS_OUTPUT.PUT_LINE( 
  'i_quantity: ' &#124;&#124; v_i_quantity ); 
 END; 
 $$ 
 ;</code></pre>
</td>
</tr>
</tbody>
</table>

#### 5.4.5 Defining Anonymous Code Blocks

**Description**

An anonymous code block generates and executes a temporary function within a procedural language.

**Functional differences**

 - **Oracle database**
     - Anonymous code blocks that are enclosed with (DECLARE) BEGIN to END can be executed.
 - **PostgreSQL**
     - PL/pgSQL blocks ((DECLARE) BEGIN to END) that are enclosed with DO $$ to $$ can be executed.

**Migration procedure**

Use the following procedure to perform migration:

 1. Search for the keywords DECLARE and BEGIN, and identify where an anonymous code block is defined.
 2. Specify DO $$ at the beginning of the anonymous code block.
 3. Delete the final slash (/) and specify $$.

**Migration example**

The example below shows migration when an anonymous code block is defined.

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
 BEGIN 
<br>
     DBMS_OUTPUT.PUT_LINE( 'Hello World.' ); 
 END; 
 / 
 </code></pre>
</td>

<td align="left">
<pre><code>DO $$ 
 BEGIN 
     PERFORM DBMS_OUTPUT.SERVEROUTPUT( TRUE ); 
     PERFORM DBMS_OUTPUT.PUT_LINE( 'Hello World.' ); 
 END; 
 $$ 
 ;</code></pre>
</td>
</tr>
</tbody>
</table>

### 5.5 Migrating Packages

This section explains how to migrate PL/SQL packages.

**Description**

A package defines and contains procedures and functions as a single relationship group in the database.

**Functional differences**

 - **Oracle database**
     - Packages can be created.
 - **PostgreSQL**
     - Packages cannot be created.

Packages cannot be created in PostgreSQL, so define a schema with the same name as the package and define functions that have a relationship in the schema so that they are treated as a single group.
In the following sections, the migration procedure is explained for each feature to be defined in a package.

#### 5.5.1 Defining Functions Within a Package

**Functional differences**

 - **Oracle database**
     - Functions can be created within a package.
 - **PostgreSQL**
     - The package itself cannot be created.

**Migration procedure**

Use the following procedure to perform migration:

 1. Search for the keywords CREATE and PACKAGE, and identify where they are defined.
 2. Define a schema with the same name as the package.
 3. If a FUNCTION statement is specified within a CREATE PACKAGE BODY statement, define, within the schema created in step 2, the functions that were defined within the package.

**Migration example**

The example below shows migration when a package is defined and functions are created within that package.

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
<pre><code><b>CREATE PACKAGE smpl_pkg AS 
   FUNCTION remove_row( rm_i_name VARCHAR2 ) 
     RETURN INTEGER; 
 END smpl_pkg; 
 / 
 CREATE PACKAGE BODY smpl_pkg AS</b> 
   <b>FUNCTION</b> remove_row( rm_i_name VARCHAR2 ) 
     <b>RETURN</b> INTEGER <b>IS</b> 
<br>
       rtn_row INTEGER; 
     BEGIN 
       DELETE FROM inventory_table 
 WHERE i_name = rm_i_name; 
<br>
       RETURN(SQL%ROWCOUNT); 
     END; 
 <b>END smpl_pkg;</b> 
 /</code></pre>
</td>

<td align="left">
<pre><code><b>CREATE SCHEMA smpl_scm;</b> 
<br>
<br>
<br>
<br>
 <b>CREATE FUNCTION smpl_scm.</b>remove_row( 
  rm_i_name VARCHAR 
 ) <b>RETURNS</b> INTEGER <b>AS $$</b> 
 <b>DECLARE</b> 
   rtn_row INTEGER; 
 BEGIN 
   DELETE FROM inventory_table 
     WHERE i_name = rm_i_name; 
   GET DIAGNOSTICS rtn_row := ROW_COUNT; 
   RETURN rtn_row; 
 END; 
 <b>$$ LANGUAGE plpgsql;</b> 
 </code></pre>
</td>
</tr>
</tbody>
</table>

**See**

----

Refer to "Defining Functions" for information on migrating FUNCTION statements within a package.

----

#### 5.5.2 Defining Procedures Within a Package

**Functional differences**

 - **Oracle database**
     - Procedures can be created within a package.
 - **PostgreSQL**
     - The package itself cannot be created.

**Migration procedure**

Use the following procedure to perform migration:

 1. Search for the keywords CREATE and PACKAGE, and identify where they are defined.
 2. Define a schema with the same name as the package.
 3. If a PROCEDURE statement is specified within a CREATE PACKAGE BODY statement, migrate the procedures that were defined within the package to functions and define them within the schema created in step 2.

**Migration example**

The example below shows migration when a package is defined and procedures are created within that package.

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
<pre><code><b>CREATE PACKAGE smpl_pkg AS 
  PROCEDURE increase_row( 
   add_i_num SMALLINT, 
   add_i_name VARCHAR2, 
   add_i_quantity INTEGER, 
   add_i_warehouse SMALLINT 
  ); 
  END smpl_pkg; 
 / 
 CREATE PACKAGE BODY smpl_pkg AS</b> 
  <b>PROCEDURE</b> increase_row( 
   add_i_num SMALLINT, 
   add_i_name VARCHAR2, 
   add_i_quantity INTEGER, 
   add_i_warehouse SMALLINT 
  ) <b>IS</b> 
   BEGIN 
    INSERT INTO inventory_table 
     VALUES ( add_i_num, 
              add_i_name, 
              add_i_quantity, 
              add_i_warehouse ); 
     END; 
 <b>END smpl_pkg;</b> 
 /</code></pre>
</td>

<td align="left">
<pre><code><b>CREATE SCHEMA smpl_scm;</b> 
<br>
<br>
<br>
<br>
<br>
<br>
<br>
<br>
<br>
 <b>CREATE FUNCTION smpl_scm.</b>increase_row( 
  add_i_num SMALLINT, 
  add_i_name VARCHAR, 
  add_i_quantity INTEGER, 
  add_i_warehouse SMALLINT 
 ) <b>RETURNS VOID AS $$</b> 
 BEGIN 
  INSERT INTO inventory_table 
   VALUES ( add_i_num, 
            add_i_name, 
            add_i_quantity, 
            add_i_warehouse ); 
 END; 
 <b>$$ LANGUAGE plpgsql;</b> 
 </code></pre>
</td>
</tr>
</tbody>
</table>

**See**

----

Refer to "Defining Procedures" for information on migrating PROCEDURE statements within a package.

----

#### 5.5.3 Sharing Variables Within a Package

**Functional differences**

 - **Oracle database**
     - Variables can be shared within a package.
 - **PostgreSQL**
     - A package cannot be created, so variables cannot be shared.

**Migration procedure**

Use a temporary table instead of variables within a package. Use the following procedure to perform migration:

 1. Search for the keywords CREATE and PACKAGE, and identify where they are defined.
 2. Check for variables defined directly in a package.
 3. Create a temporary table that defines the variables checked in step 2 in a column.
 4. Insert one record to the temporary table created in step 3. (The set value is the initial value specified within the package.)
 5. Replace the respective portions that reference a variable and set a variable with SQL statements.
     - To reference a variable, use a SELECT INTO statement to store a value in the variable and then reference it. (A variable for referencing a variable must be defined separately.)
     - To update a variable, use an UPDATE statement and update the target column.

**Migration example**

The example below shows migration when a package is defined and variables within the package are shared.

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
 CREATE PACKAGE row_pkg AS 
   PROCEDURE set_item( item INTEGER ); 
   i_item INTEGER; 
 END row_pkg; 
 / 
 CREATE PACKAGE BODY row_pkg AS 
<br>
 PROCEDURE set_item( 
 item INTEGER 
 ) IS 
     BEGIN 
       i_item := item; 
     END; 
 END row_pkg; 
 / 
 ------------------------------------------------- 
 SET SERVEROUTPUT ON; 
<br>
<br>
<br>
<br>
<br>
<br>
 BEGIN 
<br>
   row_pkg.set_item( 1000 ); 
<br>
<br>
   DBMS_OUTPUT.PUT_LINE( 
          'ITEM :' &#124;&#124; <b>row_pkg.i_item</b> ); 
   row_pkg.set_item(2000); 
<br>
<br>
   DBMS_OUTPUT.PUT_LINE( 
          'ITEM :' &#124;&#124; <b>row_pkg.i_item</b> ); 
 END; 
 / 
 </code></pre>
</td>

<td align="left">
<pre><code>CREATE SCHEMA row_pkg; 
<br>
<br>
<br>
<br>
<br>
<br>
<br>
 CREATE FUNCTION row_pkg.set_item( 
 item INTEGER 
 ) RETURNS VOID AS $$ 
   BEGIN 
     <b>UPDATE row_pkg_variables SET i_item = item;</b> 
   END; 
 $$ LANGUAGE plpgsql; 
<br>
 ------------------------------------------------- 
 <b>CREATE TEMP TABLE row_pkg_variables ( i_item INTEGER ); 
 INSERT INTO row_pkg_variables VALUES (0);</b> 
<br>
 DO $$ 
 <b>DECLARE 
   g_item INTEGER;</b> 
 BEGIN 
   PERFORM DBMS_OUTPUT.SERVEROUTPUT( TRUE ); 
   PERFORM row_pkg.set_item( 1000 ); 
   SELECT i_item INTO g_item 
     FROM row_pkg_variables; 
   PERFORM DBMS_OUTPUT.PUT_LINE( 
                  'ITEM :' &#124;&#124; <b>g_item</b> ); 
   PERFORM row_pkg.set_item(2000); 
   <b>SELECT i_item INTO g_item 
     FROM row_pkg_variables;</b> 
   PERFORM DBMS_OUTPUT.PUT_LINE( 
                  'ITEM :' &#124;&#124; <b>g_item</b> ); 
 END; 
 $$ 
 ; </code></pre>
</td>
</tr>
</tbody>
</table>

