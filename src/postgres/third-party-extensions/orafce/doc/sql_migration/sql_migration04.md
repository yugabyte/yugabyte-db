Chapter 4 Migrating SQL Statements
---

This chapter explains how to migrate SQL statements.

### 4.1 Partitions

This section provides examples of migrating partitions.

#### 4.1.1 Partition Tables

**Description**

Partitions split tables and indexes into smaller units for management.

The following example shows conversion of an Oracle database partition to child tables in PostgreSQL. All migration examples provided here are based on this table.

**Example of tables created by partitioning inventory_table**

|	i_number <br> (product code)	|	i_name <br> (category)	|	i_quantity <br> (inventory quantity)	|	i_warehouse <br> (warehouse code)	|
|	:---:	|	:---	|	:---:	|	:---:	|
|	SMALLINT <br> PRIMARY KEY	|	VARCHAR(20) <br> NOT NULL	|	INTEGER	|	SMALLINT	|
|	123	|	refrigerator	|	60	|	1	|
|	124	|	refrigerator	|	75	|	1	|
|	226	|	refrigerator	|	8	|	1	|
|	227	|	refrigerator	|	15	|	1	|
|	110	|	television	|	85	|	2	|
|	111	|	television	|	90	|	2	|
|	140	|	cd player	|	120	|	2	|
|	212	|	television	|	0	|	2	|
|	215	|	Video	|	5	|	2	|
|	240	|	cd player	|	25	|	2	|
|	243	|	cd player	|	14	|	2	|
|	351	|	Cd	|	2500	|	2	|


**Functional differences**

 - **Oracle database**
     - Partition tables can be created.
 - **PostgreSQL**
     - Partition tables cannot be created.

**Migration procedure**

Use the following procedure to perform migration:

 1. Search for the keyword PARTITION and identify where CREATE TABLE is used to create a partition.
 2. Delete the PARTITION clause and following lines from the CREATE TABLE statement and create a table.
 3. Create a child table that inherits the table defined in step 1, and add table constraints to the split table for defining partition constraints.
 4. Define a trigger or rule so that data inserted to the table is assigned to the appropriate child table.

**Migration example**

The example below shows migration when partitions are created in inventory_table.

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
<pre><code>CREATE TABLE inventory_table( 
 i_number SMALLINT PRIMARY KEY, 
 i_name VARCHAR2(20) NOT NULL, 
 i_quantity INTEGER, 
 i_warehouse SMALLINT ) 
 <b>PARTITION BY LIST ( i_warehouse ) 
 ( PARTITION inventory_table_1 VALUES (1), 
 PARTITION inventory_table_2 VALUES (2) );</b> 
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
 <pre><code>CREATE TABLE inventory_table( 
 i_number SMALLINT PRIMARY KEY, 
 i_name VARCHAR(20) NOT NULL, 
 i_quantity INTEGER, 
 i_warehouse SMALLINT ); 
 <b>CREATE TABLE inventory_table_1 
 (CHECK ( i_warehouse = 1 )) 
 INHERITS( inventory_table ); 
 CREATE TABLE inventory_table_2 
 (CHECK ( i_warehouse = 2 )) 
 INHERITS( inventory_table ); 
 ------------------------------------------------- 
 CREATE FUNCTION TABLE_INSERT_TRIGGER() 
 RETURNS TRIGGER AS $$ 
 BEGIN 
   IF ( NEW.i_warehouse = 1 ) THEN 
     INSERT INTO inventory_table_1 
 VALUES( NEW.*); 
   ELSIF ( NEW.i_warehouse = 2 ) THEN 
     INSERT INTO inventory_table_2 
 VALUES( NEW.*); 
 ELSE 
   RAISE EXCEPTION 'Data out of range.  Fix the TABLE_INSERT_TRIGGER() function!'; 
   END IF; 
   RETURN NULL; 
 END; 
 $$ LANGUAGE plpgsql; 
 ------------------------------------------------- 
 CREATE TRIGGER TABLE_INSERT_TRIGGER 
 BEFORE INSERT ON inventory_table 
 FOR EACH ROW 
 EXECUTE PROCEDURE TABLE_INSERT_TRIGGER();</b></code></pre>
</td>
</tr>
</tbody>
</table>


#### 4.1.2 PARTITION Clause in a SELECT Statement

Before a PARTITION clause in a SELECT statement can be migrated, the Oracle database partition must have been converted to child tables for PostgreSQL.

**Description**

A PARTITION clause treats only some partitions of the table (partition table) specified in the FROM clause as the targets of a query.

#### 4.1.2.1 Queries Where a PARTITION Clause is Specified

**Functional differences**

 - **Oracle database**
     - A PARTITION clause can be specified.
 - **PostgreSQL**
     - A PARTITION clause cannot be specified.

**Migration procedure**

A PARTITION clause cannot be specified, so change the search target to a child table so that the same result is returned. Use the procedure below to perform migration. The migration sequence depends on whether a FOR clause is specified.

1. Search for the keyword PARTITION and identify where it is specified in a SELECT statement.
2. Change the table name specified in the FROM clause to the name of the child table corresponding to the partition specified in the PARTITION clause.
3. Delete the PARTITION clause.

**Migration example**

The example below shows migration of a query that uses PARTITION.

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
<pre><code>SELECT * 
 FROM <b>inventory_table PARTITION( 
 inventory_table_1)</b>;</code></pre>
</td>

<td align="left">
<pre><code>SELECT * 
 FROM <b>inventory_table_1</b>; 
 </code></pre>
</td>
</tr>
</tbody>
</table>

#### 4.1.2. Queries when FOR is Specified in a PARTITION Clause

**Functional differences**

 - **Oracle database**
     - FOR can be specified in a PARTITION clause.
 - **PostgreSQL**
     - A PARTITION clause cannot be specified.

**Migration procedure**

Use the following procedure to perform migration:

1. Search for the keyword PARTITION and identify where it is specified in a SELECT statement.
2. Move the value specified in the PARTITION clause to a WHERE clause, and replace it with a conditional expression that compares the value with the column name written in the partition definition.
3. Delete the PARTITION clause.

**Migration example**

The example below shows migration when a PARTITION FOR clause is used to execute a query.

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
<pre><code>SELECT * 
 FROM inventory_table <b>PARTITION FOR (2)</b>;</code></pre>
</td>

<td align="left">
<pre><code>SELECT * 
 FROM inventory_table <b>WHERE i_warehouse = 2</b>;</code></pre>
</td>
</tr>
</tbody>
</table>

### 4.2 SELECT Statements

This section explains SELECT statements.

#### 4.2.1 UNIQUE

**Description**

UNIQUE deletes duplicate rows from the selected list and displays the result.

**Functional differences**

 - **Oracle database**
     - UNIQUE can be specified in a select list.
 - **PostgreSQL**
     - UNIQUE cannot be specified in a select list. Specify DISTINCT instead.

**Migration procedure**

Use the following procedure to perform migration:

1. Search for the keyword UNIQUE and identify where it is specified in the select list of the SELECT statement.
2. Change UNIQUE in the SELECT statement to DISTINCT.

**Migration example**

The example below shows migration when UNIQUE is specified in a select list.

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
<pre><code>SELECT <b>UNIQUE</b> i_name 
FROM inventory_table;</code></pre>
</td>

<td align="left">
<pre><code>SELECT <b>DISTINCT</b> i_name 
FROM inventory_table;</code></pre>
</td>
</tr>
</tbody>
</table>


#### 4.2.2 Subqueries

**Description**

A subquery specifies a sub SELECT statement in the main SQL statement.

**Functional differences**

 - **Oracle database**
     - A subquery specified in a FROM clause can be executed even without an alias being specified for it.
 - **PostgreSQL**
     - A subquery specified in a FROM clause cannot be executed unless an alias is specified for it.

**Migration procedure**

Use the following procedure to perform migration:

1. Search for the keyword SELECT and identify where a subquery is used.
2. If the subquery is specified in a FROM clause and no alias is specified for it, specify an alias.

**Migration example**

The example below shows migration when a SELECT statement that uses a subquery is executed.

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
<pre><code>SELECT * 
 FROM ( SELECT * FROM inventory_table 
 WHERE i_name = 'television' ) 
 WHERE i_quantity > 0; </code></pre>
</td>

<td align="left">
<pre><code>SELECT * 
 FROM ( SELECT * FROM inventory_table 
 WHERE i_name = 'television' ) <b>AS foo</b> 
 WHERE i_quantity > 0;</code></pre>
</td>
</tr>
</tbody>
</table>


#### 4.2.3 Hierarchical Queries

**Description**

If a table contains data that can associate its own records, a hierarchical query displays the result of associating those records.

##### 4.2.3.1 Executing Hierarchical Queries

**Functional differences**

 - **Oracle database**
     - Hierarchical queries can be used.
 - **PostgreSQL**
     - Hierarchical queries cannot be used.

**Migration procedure**

Hierarchical queries cannot be used, so specify a recursive query that uses a WITH clause so that the same result is returned. Use the following procedure to perform migration:

1. Search for the keyword CONNECT and identify where a hierarchical query is used.
2. Check the following:
     - Target table of the hierarchical query
     - Column being used
     - Conditional expressions specified in the CONNECT BY clause
3. Replace the hierarchical query with WITH clause syntax to match the format shown below.
4. Change the table name specified in the FROM clause to the name of the query in the WITH clause, and delete the CONNECT BY clause.

~~~
WITH RECURSIVE queryName(
     columnUsed ) AS
( SELECT columnUsed
      FROM  targetTableOfHierarchicalQuery
    UNION ALL
    SELECT columnUsed(qualified by n)
      FROM  targetTableOfHierarchicalQuery  n,
            queryName  w
      WHERE conditionalExprOfConnectByClause(use w to qualify part qualified by PRIOR) )
~~~

**Migration example**

The example below shows migration when a hierarchical query is executed.

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
<pre><code>SELECT staff_id, name, manager_id 
 FROM staff_table 
 CONNECT BY PRIOR staff_id = manager_id; 
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
<pre><code>WITH RECURSIVE staff_table_w( staff_id, 
 name, 
 manager_id ) AS 
 ( SELECT staff_id, name, manager_id 
       FROM staff_table 
     UNION ALL 
     SELECT n.staff_id, n.name, n.manager_id 
       FROM staff_table n, staff_table_w w 
       WHERE w.staff_id = n.manager_id ) 
 SELECT staff_id, name, manager_id 
 FROM staff_table_w;</code></pre>
</td>
</tr>
</tbody>
</table>

##### 4.2.3.2 Hierarchical Query with Start Row

**Functional differences**

 - **Oracle database**
     - A START WITH clause can be specified in a hierarchical query to set start row conditions.
 - **PostgreSQL**
     - A START WITH clause cannot be specified.

**Migration procedure**

In a recursive query that uses a WITH clause, set a condition that is self-referencing so that the same result is returned. Use the following procedure to perform migration:

1. Replace the hierarchical query with syntax that uses a recursive query (WITH clause).
2. If a START WITH clause is specified, move the specified conditional expression to the WHERE clause of the first subquery specified in the WITH clause.
3. Delete the START WITH clause.

**Migration example**

The example below shows migration when the start row is specified using a hierarchical query.

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
<pre><code>SELECT staff_id, name, manager_id 
 FROM staff_table 
 <b>START WITH staff_id = '1001'</b> 
 CONNECT BY PRIOR staff_id = manager_id;
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
<pre><code>WITH RECURSIVE staff_table_w( staff_id, 
 name, 
 manager_id ) AS 
 ( SELECT staff_id, name, manager_id 
       FROM staff_table 
       <b>WHERE staff_id = '1001'</b> 
     UNION ALL 
     SELECT n.staff_id, n.name, n.manager_id 
       FROM staff_table n, staff_table_w w 
       WHERE w.staff_id = n.manager_id ) 
 SELECT staff_id, name, manager_id 
 FROM staff_table_w;</code></pre>
</td>
</tr>
</tbody>
</table>

##### 4.2.3.3 Hierarchical Query Displaying the Hierarchical Position of Each Row

**Functional differences**

 - **Oracle database**
     - Specifying a LEVEL pseudocolumn in the select list of a hierarchical query displays the hierarchical position of each row.
 - **PostgreSQL**
     - A LEVEL pseudocolumn cannot be specified.

**Migration procedure**

In a recursive query that uses a WITH clause, create a result column equivalent to the LEVEL pseudocolumn as the query result of the WITH clause so that the same result is returned. Use the following procedure to perform migration:

1. Replace the hierarchical query with syntax that uses a recursive query (WITH clause).
2. Add LEVEL to the column list of the query result of the WITH clause.
3. Specify the following values as the select list of the subquery in the position corresponding to the LEVEL column:
     - Specify 1 in the first query.
     - Specify LEVEL + 1 in the next query. (LEVEL is a column of the recursive table.)
4. Using a query, replace the portion where the LEVEL pseudocolumn is used with LEVEL (quoted identifier).


The following shows the conversion format containing the LEVEL pseudocolumn.

~~~
WITH RECURSIVE queryName(
     columnUsed, "LEVEL"
) AS
( SELECT columnUsed, 1
      FROM  targetTableOfHierarchicalQuery
    UNION ALL
    SELECT columnUsed(qualified by n), w."LEVEL" + 1
      FROM  targetTableOfHierarchicalQuery  n,
            queryName  w
      WHERE conditionalExprOfConnectByClause(use w to qualify part qualified by PRIOR) )
~~~


**Migration example**

The example below shows migration when a hierarchical query is used for displaying the hierarchical position of each row.

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
<pre><code>SELECT staff_id, name, manager_id, <b>LEVEL</b> 
 FROM staff_table 
 START WITH staff_id = '1001' 
 CONNECT BY PRIOR staff_id = manager_id; 
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
<pre><code>WITH RECURSIVE staff_table_w( staff_id, 
                name, 
                manager_id, 
                <b>"LEVEL"</b>  ) AS 
   ( SELECT staff_id, name, manager_id, <b>1</b> 
       FROM staff_table 
       WHERE staff_id = '1001' 
     UNION ALL 
     SELECT n.staff_id, 
            n.name, 
            n.manager_id, 
            <b>w."LEVEL" + 1</b> 
       FROM staff_table n, staff_table_w w 
       WHERE w.staff_id = n.manager_id ) 
 SELECT staff_id, name, manager_id, <b>"LEVEL"</b>  
 FROM staff_table_w;</code></pre>
</td>
</tr>
</tbody>
</table>

##### 4.2.3.4 Hierarchical Query Displaying the Hierarchical Structure

**Functional differences**

 - **Oracle database**
     - Specifying SYS_CONNECT_BY_PATH in the select list of a hierarchical query displays the hierarchical structure.
 - **PostgreSQL**
     - SYS_CONNECT_BY_PATH cannot be specified.

**Migration procedure**

In a recursive query that uses a WITH clause, create an arithmetic expression that also uses the recursive query of the WITH clause so that the same result is returned. Use the following procedure to perform migration:

1. Replace the hierarchical query with syntax that uses a recursive query (WITH clause).
2. Add PATH to the column list of the query result of the WITH clause.
3. Specify the following values for the select list of the subquery corresponding to the PATH column. The example here explains migration when a slash (/) is specified as the delimiter.
-	In the first query, specify the path to the values of the columns from the root to the node.
-	Specify m.PATH || '/' || n.columnName in the next query. (PATH is a recursive table column.)
4. Using a query, replace the part where PATH is used, with PATH.

The following shows the conversion format containing PATH.

~~~
WITH RECURSIVE queryName(
     columnUsed, PATH
) AS
( SELECT columnUsed, '/' || columnName
      FROM  targetTableOfHierarchicalQuery
    UNION ALL
    SELECT columnUsed(qualified by n), w.PATH || '/' || n.columnName
      FROM  targetTableOfHierarchicalQuery  n,
            queryName  w
      WHERE conditionalExprOfConnectByClause )
~~~

For conditionalExprOfConnectByClause, use w to qualify the part qualified by PRIOR.

**Migration example**

The example below shows migration when the hierarchical structure is displayed.

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
<pre><code>SELECT staff_id,name,manager_id, 
 <b>SYS_CONNECT_BY_PATH(name,'/') AS PATH </b> 
 FROM staff_table 
 START WITH staff_id = '1001' 
 CONNECT BY PRIOR staff_id = manager_id; 
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
<pre><code> WITH RECURSIVE staff_table_w( staff_id, 
                name, 
                manager_id, 
                 <b>PATH</b>) AS 
 ( SELECT staff_id,name,manager_id, <b>'/' &#124;&#124; name</b> 
 	FROM staff_table 
 	WHERE staff_id = '1001' 
 	UNION ALL 
 	SELECT n.staff_id, 
               n.name, 
               n.manager_id, 
               <b>w.PATH &#124;&#124; '/' &#124;&#124; n.name</b> 
 	FROM staff_table n,staff_table_w w 
 	WHERE w.staff_id = n.manager_id ) 
 SELECT staff_id,name,manager_id, <b>PATH</b> 
 FROM staff_table_w;</code></pre>
</td>
</tr>
</tbody>
</table>


##### 4.2.3.5 Hierarchical Queries That Sort Each Hierarchy Level

**Functional differences**

 - **Oracle database**
     - Specifying an ORDER SIBLINGS BY clause in a hierarchical query enables sorting of each hierarchical level.
 - **PostgreSQL**
     - An ORDER SIBLINGS BY clause cannot be specified.

**Migration procedure**

Use the following procedure to perform migration:

1. Replace the hierarchical query with syntax that uses a recursive query (WITH clause).
2. In the syntax that uses a recursive query (WITH clause), add poskey to the column list of the query result of the WITH clause.
3. Specify ROW_NUMBER() in the position corresponding to the poskey column. In the OVER clause, specify an ORDER BY clause that specifies the column of the ORDER SIBLINGS BY clause.
4. Add siblings_pos to the column list of the query result of the WITH clause.
5. Specify the following values as the select list of the subquery in the position corresponding to the siblings_pos column:
     - Specify ARRAY[poskey] in the first query.
     - Specify a concatenation of siblings_pos and poskey in the next query.
6. Using a query, specify siblings_pos in the ORDER BY clause to perform a sort.

The following shows the conversion format containing sorting of each hierarchy level.

~~~
WITH RECURSIVE queryNameForPoskey(
     columnUsed, poskey
) AS
( SELECT columnUsed, 
ROW_NUMBER() OVER( ORDER BY columnNameSpecifiedInOrderSiblingsByClause )
      FROM  targetTableOfHierarchicalQuery ),
WITH RECURSIVE queryName(
     columnUsed, siblings_pos
) AS
( SELECT columnUsed, ARRAY[poskey]
      FROM  queryNameForPoskey
    UNION ALL
    SELECT columnUsed(qualified by n), w.siblings_pos || n.poskey
      FROM  queryNameForPoskey  n,
            queryName  w
      WHERE conditionalExprOfConnectByClause(use w to qualify part qualified by PRIOR ) )
~~~

**Migration example**

The example below shows migration when a hierarchical query is used to perform a sort in each hierarchy level.

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
<pre><code>SELECT staff_id, name, manager_id 
  FROM staff_table 
  START WITH staff_id = '1001' 
  CONNECT BY PRIOR staff_id = manager_id 
  <b>ORDER SIBLINGS BY name;</b> 
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
<br>
<br>
<br>
<br>
<br>
<br>
 </code></pre>
</td>

<td align="left">
<pre><code> WITH RECURSIVE staff_table_pos ( staff_id, 
                name, 
                manager_id, 
                poskey ) AS 
 ( <b>SELECT staff_id, 
          name, 
          manager_id, 
 ROW_NUMBER() OVER( ORDER BY name ) 
 FROM staff_table</b> ), 
 staff_table_w ( staff_id, 
                 name, 
                 manager_id, 
 <b>siblings_pos</b> ) AS 
 ( SELECT staff_id, 
          name, 
          manager_id, 
          <b>ARRAY[poskey]</b> 
         FROM staff_table_pos 
         WHERE staff_id = '1001' 
       UNION ALL 
       SELECT n.staff_id, 
              n.name, 
              n.manager_id, 
              <b>w.siblings_pos &#124;&#124; n.poskey</b> 
         FROM staff_table_pos n, staff_table_w w 
         WHERE w.staff_id = n.manager_id ) 
 SELECT staff_id, name, manager_id 
 FROM staff_table_w 
 ORDER BY <b>siblings_pos;</b></code></pre>
</td>
</tr>
</tbody>
</table>

##### 4.2.3.6 Hierarchical Query Displaying data from the root

**Functional differences**

 - **Oracle database**
     - Specifying CONNECT_BY_ROOT in the select list of a hierarchical query displays data from the root.
 - **PostgreSQL**
     - CONNECT_BY_ROOT cannot be specified.

**Migration procedure**

In a recursive query that uses a WITH clause, add a root column that also uses the recursive query of the WITH clause so that the same result is returned. Use the following procedure to perform migration:

1. Replace the hierarchical query with syntax that uses a recursive query (WITH clause).
2. Add root column to the column list of the query result of the WITH clause.

-	In the first query, specify the root columnName to the values of the columns from the root to the node.
-	Specify m.rootName in the next query. (rootName is a root column.)


The following shows the conversion format containing rootName.

~~~
WITH RECURSIVE queryName(
     columnUsed, rootName
) AS
( SELECT columnUsed, columnName
      FROM  targetTableOfHierarchicalQuery
    UNION ALL
    SELECT columnUsed(qualified by n), w.rootName
      FROM  targetTableOfHierarchicalQuery  n,
            queryName  w
      WHERE conditionalExprOfConnectByClause )
~~~

For conditionalExprOfConnectByClause, use w to qualify the part qualified by PRIOR.

**Migration example**

The example below shows migration when the root data is displayed.
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
<pre><code>SELECT staff_id, name, <b>CONNECT_BY_ROOT name as "Manager" </b>
  FROM staff_table 
  START WITH staff_id = '1001' 
  CONNECT BY PRIOR staff_id = manager_id;
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
<pre><code>WITH RECURSIVE staff_table_w( staff_id, 
 name, 
 Manager ) AS 
 (   SELECT staff_id, name, <b>name </b>
       FROM staff_table 
     UNION ALL 
     SELECT n.staff_id, n.name, <b>w.Manager </b>
       FROM staff_table n, staff_table_w w 
       WHERE w.staff_id = n.manager_id
 )
 SELECT staff_id, name, Manager 
 FROM staff_table_w;</code></pre>
</td>
</tr>
</tbody>
</table>

##### 4.2.3.7 Hierarchical Query identifys the leaves

**Functional differences**

 - **Oracle database**
     - Specifying CONNECT_BY_ISLEAF in the select list of a hierarchical query can identify the leaf rows. This returns 1 if the current row is a leaf. Otherwise it returns 0.
 - **PostgreSQL**
     - CONNECT_BY_ISLEAF cannot be specified.

**Migration procedure**

In a recursive query that uses a WITH clause, the leaf can be checked using a sub-query so that the same result is returned. Use the following procedure to perform migration:

1. Replace the hierarchical query with syntax that uses a recursive query (WITH clause).
2. Add a sub-query to the column list of the query result of the WITH clause.

The following shows the conversion format containing rootName.

~~~
 WITH RECURSIVE queryName(
     columnUsed1, columnUsed2
) AS
(   SELECT columnUsed, columnUsed2
      FROM  targetTableOfHierarchicalQuery
    UNION ALL
    SELECT columnUsed(qualified by n), columnUsed2(qualified by n)
      FROM  targetTableOfHierarchicalQuery n,
            queryName w
      WHERE conditionalExprOfConnectByClause 
)
SELECT *,
       CASE WHEN EXISTS(select * from queryName p where p.columnUsed1 = e.columnUsed2)
            THEN 0 ELSE 1 END 
        as is_leaf
 FROM queryName e;
~~~

For conditionalExprOfConnectByClause, use w to qualify the part qualified by PRIOR.

**Migration example**

The example below shows migration when the leaf data is displayed.
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
<pre><code>SELECT staff_id, name, <b>CONNECT_BY_ISLEAF </b>
  FROM staff_table 
  START WITH staff_id = '1001' 
  CONNECT BY PRIOR staff_id = manager_id;
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
<pre><code>WITH RECURSIVE staff_table_w( staff_id, 
                            name ) AS 
 (   SELECT staff_id, name
       FROM staff_table 
     UNION ALL 
     SELECT n.staff_id, n.name
       FROM staff_table n, staff_table_w w 
       WHERE w.staff_id = n.manager_id
 )
 SELECT staff_id, name,
       <b>CASE WHEN EXISTS(select 1 from staff_table_w p where p.manager_id = e.staff_id)
            THEN 0 ELSE 1 END 
        as is_leaf </b>
 FROM staff_table_w e;</code></pre>
</td>
</tr>
</tbody>
</table>

##### 4.2.3.8 Returns all possible hierarchy permutations

**Functional differences**

 - **Oracle database**
     - When CONNECT BY LEVEL is used without START WITH clause and PRIOR operator.
 - **PostgreSQL**
     - CONNECT BY LEVEL cannot be specified.

**Migration procedure**

In a recursive query that uses a WITH clause, returns all possible hierarchy permutations can use the descartes product. Use the following procedure to perform migration:

1. Replace the hierarchical query with syntax that uses a recursive query (WITH clause).
2. The left query of Union ALL does not specify a filter condition. The right query is the Descartes product of two tables, and the filter condition is the number of recursions.

The following shows the conversion format containing rootName.

~~~
 WITH RECURSIVE queryName(
     columnUsed1, level
) AS
(   SELECT columnUsed, 1
      FROM  targetTableOfHierarchicalQuery
    UNION ALL
    SELECT columnUsed(qualified by n), w.level+1
      FROM  targetTableOfHierarchicalQuery n,
            queryName w
      WHERE  w.level+1 < levelNum
);
~~~


**Migration example**

The example below shows migration when returns all possible hierarchy permutations of two levels.

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
<pre><code>select staff_id, name, level 
    from staff_table 
    <b>connect by level < 3</b>;
<br>
<br>
<br>
<br>
<br>
<br>
 </code></pre>
</td>

<td align="left">
<pre><code>WITH RECURSIVE staff_table_w( staff_id, 
                     name 
                     level ) AS 
 (   SELECT staff_id, name, <b>1 </b>
       FROM staff_table 
     UNION ALL 
     SELECT n.staff_id, n.name, <b>w.level + 1 </b>
       FROM staff_table n, staff_table_w w
       <b> WHERE w.level + 1 < 3 </b>
 )
 SELECT staff_id, name, level 
 FROM staff_table_w;</code></pre>
</td>
</tr>
</tbody>
</table>

#### 4.2.4 MINUS

**Description**

MINUS finds the difference between two query results, that is, it returns rows that are in the first result set that are not in the second result set.

**Functional differences**

 - **Oracle database**
     - MINUS is specified to find the difference between two query results.
 - **PostgreSQL**
     - MINUS cannot be specified to find the difference between two query results. Specify EXCEPT instead.

**Migration procedure**

Use the following procedure to perform migration:

1. Search for the keyword MINUS and identify where it is used.
2. Change MINUS to EXCEPT.

**Migration example**

The example below shows migration when the set difference of query results is to be found.

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
<pre><code>SELECT i_number, i_name FROM inventory_table 
  WHERE i_warehouse = 2 
 <b>MINUS</b> 
 SELECT i_number, i_name FROM inventory_table 
  WHERE i_name = 'cd';</code></pre>
</td>

<td align="left">
<pre><code>SELECT i_number, i_name FROM inventory_table 
  WHERE i_warehouse = 2 
 <b>EXCEPT</b> 
 SELECT i_number, i_name FROM inventory_table 
  WHERE i_name = 'cd';</code></pre>
</td>
</tr>
</tbody>
</table>


### 4.3 DELETE Statements
This section explains DELETE statements.

#### 4.3.1 Omitting the FROM Keyword

**Functional differences**

 - **Oracle database**
     - The FROM keyword can be omitted from a DELETE statement.
 - **PostgreSQL**
     - The FROM keyword cannot be omitted from a DELETE statement.

**Migration procedure**

Use the following procedure to perform migration:

1. Search for the keyword DELETE and identify where it is used.
2. If the FROM keyword has been omitted from the DELETE statement, add it.

**Migration example**

The example below shows migration when the FROM keyword is omitted from a DELETE statement.

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
<pre><code>DELETE inventory_table 
  WHERE i_name = 'cd player';</code></pre>
</td>

<td align="left">
<pre><code>DELETE <b>FROM</b> inventory_table 
  WHERE i_name = 'cd player';</code></pre>
</td>
</tr>
</tbody>
</table>



### 4.4 MERGE Statements
This section explains MERGE statements.

#### 4.4.1 Executing MERGE Statements

**Functional differences**

 - **Oracle database**
     - MERGE statements can be used.
 - **PostgreSQL**
     - MERGE statements cannot be used.

**Migration procedure**

Use the following procedure to perform migration:

1. Use an INSERT statement to specify the INSERT keyword that follows WHEN NOT MATCHED THEN in the MERGE statement.
2. Use a SELECT statement after the lines added in step 1 to specify the SELECT statement that follows the USING clause of the MERGE statement.
3. Use DO UPDATE in an ON CONFLICT clause of the INSERT statement specified in step 1 to specify the UPDATE keyword that follows WHEN MATCHED THEN in the MERGE statement.

**Migration example**

The example below shows how to migrate the MERGE statement.

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
<pre><code><b>MERGE INTO new_inventory_table N 
 USING ( SELECT i_number, 
 i_name, 
 i_quantity, 
 i_warehouse 
 FROM inventory_table ) I 
 ON ( N.i_number = I.i_number ) 
 WHEN MATCHED THEN 
     UPDATE SET N.i_quantity = I.i_quantity 
 WHEN NOT MATCHED THEN 
     INSERT ( N.i_number, 
 N.i_name, 
 N.i_quantity, 
 N.i_warehouse ) 
     VALUES ( I.i_number, 
 I.i_name, 
 I.i_quantity, 
 I.i_warehouse );</b></code></pre>
</td>

<td align="left">
<pre><code><b>INSERT INTO new_inventory_table AS N ( 
             i_number, 
             i_name, 
             i_quantity, 
             i_warehouse 
            ) 
 SELECT i_number, 
        i_name, 
        i_quantity, 
        i_warehouse 
 FROM inventory_table 
 ON CONFLICT (i_number) DO UPDATE 
 SET i_quantity = excluded.i_quantity;</b> 
<br>
<br>
<br>
<br>
 </code></pre>
</td>
</tr>
</tbody>
</table>

**Note**

----

In the migration example shown above, a primary key or unique key definition must have been specified in the column specified in the ON clause of the MERGE statement. If using a table for which a primary key or unique key definition is not specified in the column of the conditional expression, refer to the migration example provided in "Information" below.

----

**Information**

----

The example below shows migration when a primary key or unique key definition is not specified in the column specified in the ON clause of the MERGE statement.

**Migration procedure**

Use the following procedure to perform migration:

1. Use a SELECT statement in a WITH query to specify the SELECT statement that follows the USING clause of the MERGE statement.
2. Use an UPDATE statement in the WITH query to specify the UPDATE keyword that follows WHEN MATCHED THEN in the MERGE statement.
3. Specify the INSERT keyword that follows the WHEN NOT MATCHED THEN clause of the MERGE statement as an INSERT statement following the WITH query.
4. Specify NOT IN within the INSERT statement added in step 3 as an equivalent condition to the WHEN MATCHED THEN clause of the MERGE statement.

**Migration example**

The example below shows migration of a MERGE statement in which no primary key or unique key definition is specified.

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
<pre><code><b>MERGE INTO new_inventory_table N 
 USING ( SELECT i_number, 
                i_name, 
                i_quantity, 
                i_warehouse 
 FROM inventory_table ) I 
 ON ( N.i_number = I.i_number ) 
 WHEN MATCHED THEN 
     UPDATE SET N.i_quantity = I.i_quantity 
 WHEN NOT MATCHED THEN 
     INSERT ( N.i_number, 
              N.i_name, 
              N.i_quantity, 
              N.i_warehouse ) 
     VALUES ( I.i_number, 
              I.i_name, 
              I.i_quantity, 
              I.i_warehouse );</b></code></pre>
</td>

<td align="left">
<pre><code><b>WITH I AS ( 
 SELECT i_number, 
        i_name, 
        i_quantity, 
        i_warehouse 
 FROM inventory_table ), 
 U AS ( 
 UPDATE new_inventory_table AS N  
 SET i_quantity = I.i_quantity  
 FROM inventory_table I 
 WHERE N.i_number = I.i_number 
 RETURNING N.i_number ) 
 INSERT INTO new_inventory_table ( 
 SELECT * FROM I WHERE i_number NOT IN ( 
 SELECT i_number FROM U ) );</b> 
<br>
<br>
 </code></pre>
</td>
</tr>
</tbody>
</table>

----

### 4.5 ALTER INDEX Statements

**Description**

An ALTER INDEX statement changes an index definition.

#### 4.5.1 Restructuring an Index

**Functional differences**

 - **Oracle database**
     - A REBUILD clause can be specified in the ALTER INDEX statement to restructure an index.
 - **PostgreSQL**
     - A REBUILD clause cannot be specified in the ALTER INDEX statement. Use a REINDEX statement instead.

**Migration procedure**

Use the following procedure to perform migration:

1. Search for the keywords ALTER and INDEX, and identify where they are used.
2. If a REBUILD clause is specified, replace the ALTER INDEX statement with a REINDEX statement.

**Migration example**

The example below shows migration when an index is restructured.

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
<pre><code><b>ALTER</b> INDEX idx <b>REBUILD;</b></code></pre>
</td>

<td align="left">
<pre><code><b>REINDEX</b> INDEX idx;</code></pre>
</td>
</tr>
</tbody>
</table>

#### 4.5.2 Restructuring an Index Where a Tablespace is Specified

**Functional differences**

 - **Oracle database**
     - A tablespace can be specified in a REBUILD clause.
 - **PostgreSQL**
     - A REBUILD clause cannot be used.

**Migration procedure**

The REBUILD statement for an index restructure is replaced with a REINDEX statement, but a tablespace cannot be specified in this statement. Therefore, move the tablespace before performing the index restructure. Use the following procedure to perform migration:

1. Search for the keywords ALTER and INDEX, and identify where they are used.
2. If both a REBUILD clause and a TABLESPACE clause are specified, replace the REBUILD clause of the ALTER INDEX statement with a SET clause.
3. Add a REINDEX statement after the ALTER INDEX statement.

**Migration example**

The example below shows migration when a tablespace is specified for restructuring an index.

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
<pre><code>ALTER INDEX idx <b>REBUILD TABLESPACE tablespace1;</b>
</code></pre>
</td>

<td align="left">
<pre><code>ALTER INDEX idx <b>SET TABLESPACE tablespace1; 
 REINDEX INDEX idx;</b></code></pre>
</td>
</tr>
</tbody>
</table>


#### 4.5.3 Restructuring an Index Where a Free Space Percentage is Specified

**Functional differences**

 - **Oracle database**
     - PCTFREE can be specified in a REBUILD clause to specify a free space percentage for an index.
 - **PostgreSQL**
     - A REBUILD clause cannot be used.

**Migration procedure**

The REBUILD statement for an index restructure is replaced with a REINDEX statement, but a free space percentage cannot be specified in this statement. Therefore, change the index utilization rate so that an equivalent result is returned. Then restructure the index. Use the following procedure to perform migration:

1. Search for the keywords ALTER and INDEX, and identify where they are used.
2. If both a REBUILD clause and the PCTFREE keyword are specified, replace the REBUILD clause with a SET clause and change PCTFREE to FILLFACTOR. Use 100 - valueSpecifiedInPctfree as the set value.
3. Add a REINDEX statement after the ALTER INDEX statement.

**Migration example**

The example below shows migration when a fill factor is specified for restructuring an index.

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
<pre><code>ALTER INDEX idx <b>REBUILD PCTFREE 10;</b></code></pre>
</td>

<td align="left">
<pre><code>ALTER INDEX idx <b>SET (FILLFACTOR=90); 
 REINDEX INDEX idx;</b></code></pre>
</td>
</tr>
</tbody>
</table>

### 4.6 ALTER SESSION Statements

**Description**

An ALTER SESSION statement specifies and changes parameters per session.

#### 4.6.1 Closing dblink

**Functional differences**

 - **Oracle database**
     - An ALTER SESSION statement is used to close dblink.
 - **PostgreSQL**
     - ALTER SESSION statements cannot be used. Use DBLINK_CLOSE instead.

**Migration procedure**

Use the following procedure to perform migration:

1. Search for the keywords ALTER and SESSION, and identify where they are used.
2. If a CLOSE DATABASE LINK clause is specified, delete the ALTER SESSION statement and replace it with a SELECT statement that calls DBLINK_CLOSE.

**Migration example**

The example below shows migration when dblink is closed.

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
<pre><code><b>ALTER SESSION CLOSE DATABASE LINK dblink1;</b></code></pre>
</td>

<td align="left">
<pre><code><b>SELECT DBLINK_CLOSE ( 'dblink1' );</b></code></pre>
</td>
</tr>
</tbody>
</table>

#### 4.6.2 Changing the Initialization Parameters

**Functional differences**

 - **Oracle database**
     - An ALTER SESSION statement is used to change the initialization parameters.
 - **PostgreSQL**
     - ALTER SESSION statements cannot be used. Use a SET statement instead to change the server parameters.

**Migration procedure**

Use the following procedure to perform migration:

1. Search for the keywords ALTER and SESSION, and identify where they are used.
2. Replace the ALTER SESSION statement with a SET statement. The table below lists the corresponding initialization parameters and server parameters.

**Corresponding initialization parameters and server parameters**

|Initialization parameter|Runtime configuration parameters of PostgreSQL|
|:---|:---|
|	ENABLE_DDL_LOGGING	|	log_statement	|
|	NLS_CURRENCY	|	lc_monetary	|
|	NLS_DATE_FORMAT	|	DateStyle	|
|	NLS_DATE_LANGUAGE	|	lc_time	|
|	NLS_TIMESTAMP_FORMAT	|	lc_time	|
|	NLS_TIMESTAMP_TZ_FORMAT	|	lc_time	|
|	OPTIMIZER_INDEX_COST_ADJ	|	cpu_index_tuple_cost <br> seq_page_cost	|



**Migration example**

The example below shows migration when the initialization parameters are changed.

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
<pre><code><b>ALTER SESSION SET ENABLE_DDL_LOGGING = TRUE;</b></code></pre>
</td>

<td align="left">
<pre><code><b>SET log_statement = 'DDL';</b></code></pre>
</td>
</tr>
</tbody>
</table>

**Note**

----

The values that can be specified for server parameters may differ from those that can be specified for the initialization parameters. Refer to the manual provided by the vendor for the values that can be specified.

----

**See**

----

Refer to "Server Configuration" in "Server Administration" in the PostgreSQL Documentation for information on server parameters.

----


#### 4.6.3 Setting Transaction Characteristics

**Functional differences**

 - **Oracle database**
     - An ALTER SESSION statement is used to set transaction characteristics.
 - **PostgreSQL**
     - ALTER SESSION statements cannot be used. Use a SET TRANSACTION statement instead.

**Migration procedure**

Use the following procedure to perform migration:

1. Search for the keywords ALTER and SESSION, and identify where they are used.
2. If SET ISOLATION_LEVEL is specified, replace the ALTER SESSION statement with a SET TRANSACTION statement.

**Migration example**

The example below shows migration when transaction characteristics are set.

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
<pre><code><b>ALTER SESSION SET
 ISOLATION_LEVEL = SERIALIZABLE;</b></code></pre>
</td>

<td align="left">
<pre><code><b>SET TRANSACTION 
 ISOLATION LEVEL SERIALIZABLE;</b></code></pre>
</td>
</tr>
</tbody>
</table>

#### 4.6.4 Migrating the Time Zone Setting

**Functional differences**

 - **Oracle database**
     - An ALTER SESSION statement is used to set the time zone.
 - **PostgreSQL**
     - ALTER SESSION statements cannot be used. Use a SET statement instead.

**Migration procedure**

Use the following procedure to perform migration:

1. Search for the keywords ALTER and SESSION, and identify where they are used.
2. If SET TIME_ZONE is specified, replace the ALTER SESSION statement with a SET statement.

**Migration example**

The example below shows migration when the time zone is set.

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
<pre><code><b>ALTER SESSION SET TIME_ZONE = '+09:00';</b></code></pre>
</td>

<td align="left">
<pre><code><b>SET TimeZone='Japan';</b></code></pre>
</td>
</tr>
</tbody>
</table>


**Note**

----

Be sure to use the full time zone name when specifying the time zone in the TimeZone parameter of PostgreSQL.

----

### 4.7 GRANT Statements

**Description**

A GRANT statement defines access privileges.

#### 4.7.1 Migratability of GRANT Statement Features

The following table indicates the migratability of the GRANT statement features provided by Oracle databases to PostgreSQL.

**System privileges**

|GRANT statement features of Oracle databases|Migratability|Remarks|
|:---|:---:|:---|
|	Granting of system privileges	|	MR	|	PUBLIC cannot be specified for a grantee. <br> There are restrictions on the privileges that can be migrated.	|
|	Granting of role (role)	|	YR	|	PUBLIC cannot be specified for a grantee.	|
|	Granting of all system privileges (ALL PRIVILEGES)	|	Y	|	  	|
|	WITH ADMIN OPTION clause	|	MR	|	Only privileges that can be migrated with the GRANT statement.	|
|	WITH DELEGATE OPTION clause	|	N	|	  	|

**Object privileges**

|GRANT statement features of Oracle databases|Migratability|Remarks|
|:---|:---:|:---|
|	Granting of object privileges	|	YR	|	Columns can be specified. <br> There are restrictions on the privileges that can be migrated.	|
|	Granting of all object privileges (ALL [PRIVILEGES])	|	Y	|	Columns can be specified.	|
|	Grantee Schema object	|	Y	|	  	|
|	Grantee User	|	N	|	  	|
|	Grantee Directory	|	N	|	  	|
|	Grantee Edition	|	N	|	  	|
|	Grantee Mining model	|	N	|	  	|
|	Grantee Java source	|	N	|	  	|
|	Grantee SQL translation profile	|	N	|	  	|
|	WITH HIERARCHY OPTION clause	|	N	|	  	|
|	WITH GRANT OPTION clause	|	Y	|	  	|

**Program unit privileges**

|GRANT statement features of Oracle databases|Migratability|Remarks|
|:---|:---|:---|
|	Granting of program unit privileges	|	N	|	  	|

Y: Syntax can be migrated as is
YR: Syntax can be migrated as is with restrictions
MR: Modified syntax can be migrated with restrictions
N: Cannot be migrated


#### 4.7.2 Granting System Privileges
##### 4.7.2.1 Granting Privileges for Operating Users and Roles

**Functional differences**

 - **Oracle database**
     - A GRANT statement is used to grant privileges for creating and changing users and roles.
 - **PostgreSQL**
     - A GRANT statement cannot be used to grant privileges for creating and changing users and roles. Use an ALTER ROLE statement instead.

**Migration procedure**

Use the following procedure to perform migration:

1. Search for the keyword GRANT and identify where it is used.
2. If privileges for creating and changing users and roles are granted, replace the GRANT statement with the ALTER ROLE statement. The table below lists the migratable user and role operation privileges.

**Migratable user and role operation privileges**

 - **ROLES**

|GRANT statement in Oracle database|Corresponding ALTER ROLE statement in PostgreSQL|
|:---|:---:|
|GRANT CREATE ROLE TO *roleName* <br> GRANT ALTER ANY ROLE TO *roleName* <br> GRANT DROP ANY ROLE TO *roleName* <br> GRANT ANY ROLE TO *roleName* | ALTER ROLE *roleName* CREATEROLE;|

 - **USERS**

|GRANT statement in Oracle database|Corresponding ALTER ROLE statement in PostgreSQL|
|:---|:---:|
|GRANT CREATE USER TO *userName* <br> GRANT ALTER USER TO *userName* <br> GRANT DROP USER TO *userName*|ALTER ROLE *userName* CREATEUSER|


**Migration example**

The example below shows migration when role creation privileges are granted.

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
<pre><code><b>GRANT CREATE ROLE TO role1;</b></code></pre>
</td>

<td align="left">
<pre><code><b>ALTER ROLE role1 CREATEROLE;</b></code></pre>
</td>
</tr>
</tbody>
</table>

#### 4.7.2.2 Granting Privileges for Creating Objects

**Functional differences**

 - **Oracle database**
     - A GRANT statement is used to grant creation privileges per object.
 - **PostgreSQL**
     - A GRANT statement is used to grant object creation privileges per schema or database.

**Migration procedure**

Use the following procedure to perform migration:

1. Search for the keyword GRANT and identify where it is used.
2. If creation privileges are granted per object, replace the GRANT statement with a GRANT statement that grants creation privileges per schema or database. The table below lists the migratable object creation privileges.

**Migratable object creation privileges**

|Object|GRANT statement in Oracle database|Corresponding ALTER ROLE statement in PostgreSQL|
|:---|:---|:---|
|	MATERIALIZED VIEWS	|	GRANT CREATE MATERIALIZED VIEW TO *userName*	|	GRANT CREATE ON SCHEMA *schemaName* TO *userName*	|
|	OPERATORS	|	GRANT CREATE OPERATOR TO *userName*	|	GRANT CREATE ON SCHEMA *schemaName* TO *userName*	|
|	PROCEDURES	|	GRANT CREATE PROCEDURE TO *userName*	|	GRANT CREATE ON SCHEMA *schemaName* TO *userName*	|
|	SEQUENCES	|	GRANT CREATE SEQUENCE TO *userName*	|	GRANT CREATE ON SCHEMA *schemaName* TO *userName*	|
|	SESSIONS	|	GRANT CREATE SESSION TO *userName*	|	GRANT CONNECT ON DATABASE *databaseName* TO *userName*	|
|	TABLES	|	GRANT CREATE TABLE TO *userName*	|	GRANT CREATE ON SCHEMA *schemaName* TO *userName*	|
|	TRIGGERS	|	GRANT CREATE TRIGGER TO *userName*	|	GRANT CREATE ON SCHEMA *schemaName* TO *userName*	|
|	TYPES	|	GRANT CREATE TYPE TO *userName*	|	GRANT CREATE ON SCHEMA *schemaName* TO *userName*	|
|	VIEWS	|	GRANT CREATE VIEW TO *userName*	|	GRANT CREATE ON SCHEMA *schemaName* TO *userName*	|

**Migration example**

The example below shows migration when table creation privileges are granted.

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
<pre><code><b>GRANT CREATE TABLE TO user1;</b></code></pre>
</td>

<td align="left">
<pre><code><b>GRANT CREATE ON SCHEMA scm1 TO user1;</b></code></pre>
</td>
</tr>
</tbody>
</table>

##### 4.7.2.3 Granting Roles (with Password Setting)

**Functional differences**

 - **Oracle database**
     - When a GRANT statement is used to assign a user to a role, a password can be set at the same time.
 - **PostgreSQL**
     - When a GRANT statement is used to assign a user to a role, a password cannot be set at the same time.

**Migration procedure**

To set a password, you must specify a separate CREATE USER or ALTER USER statement and set the password in that statement. Use the following procedure to perform migration:

1. Search for the keyword GRANT and identify where it is used.
2. If an IDENTIFIED BY clause is specified, check if the target user exists.
3. If the user exists, use an ALTER USER statement to change the password. If the user does not exist, use a CREATE USER statement to create a new user and set a password.
4. Delete the clause for granting a password from the GRANT statement.

**Migration example**

The example below shows migration when role1 is granted to user1.


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
<pre><code>GRANT role1 TO user1 <b>IDENTIFIED BY PASSWORD;</b></code></pre>
</td>

<td align="left">
<pre><code> <b>CREATE USER user1 PASSWORD 'PASSWORD';</b> 
 GRANT role1 TO user1;</code></pre>
</td>
</tr>
</tbody>
</table>

#### 4.7.3 Granting Object Privileges

There is no difference in the syntax of GRANT statements with regard to object privileges that are migratable from an Oracle database. However, some object privileges cannot be used in PostgreSQL, so they must be changed to supported object privileges.

The table below lists the object privileges that can be migrated from an Oracle database to PostgreSQL.

**Migratable object privileges**

 - **Materialized view privileges**

|Name of object privilege|Change required|Remarks|
|:---|:---:|:---|
|	READ	|	Yes	|	Change to SELECT.	|
|	SELECT	|	No	|	If a FOR UPDATE clause is used in the SELECT statement, UPDATE privileges are also required.	|

 - **Operator privileges**

|Name of object privilege|Change required|Remarks|
|:---|:---:|:---|
EXECUTE	|	Yes	|	In PostgreSQL, EXECUTE privileges must be granted to a function that implements operators.|

 - **Procedure, function, and package privileges**

|Name of object privilege|Change required|Remarks|
|:---|:---:|:---|
|	EXECUTE	|	Yes	|	The FUNCTION keyword must be added before the function name.	|

 - **Sequence privileges**

|Name of object privilege|Change required|Remarks|
|:---|:---:|:---|
|	SELECT 	|	Yes	|	Change to USAGE. <br> The SEQUENCE keyword must be added before the sequence name.	|

 - **Table privileges**

|Name of object privilege|Change required|Remarks|
|:---|:---:|:---|
|	DELETE 	|	No	|		|
|	INSERT 	|	No	|		|
|	READ 	|	Yes	|	Change to SELECT.	|
|	REFERENCES 	|	No	|		|
|	SELECT 	|	No	|	If a FOR UPDATE clause is used in the SELECT statement, UPDATE privileges are also required.	|
|	UPDATE 	|	No	|		|

 - **View privileges**

|Name of object privilege|Change required|Remarks|
|:---|:---:|:---|
|	DELETE 	|	No	|		|
|	INSERT 	|	No	|		|
|	READ 	|	Yes	|	Change to SELECT.	|
|	REFERENCES 	|	No	|		|
|	SELECT 	|	No	|	If a FOR UPDATE clause is used in the SELECT statement, UPDATE privileges are also required.	|
|	UPDATE 	|	No	|		|


