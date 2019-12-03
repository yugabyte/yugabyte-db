This table documents the compatibility of YugabyteDB against a comprehensive list of PostgreSQL commands and syntax. In the first column you can find a links with the queries you'll need to test the compatibility yourself.

|  **Functional Category** | **Feature** | **Yugabyte 1.3** | **Notes** |
| :---: | :--- | :---: | :---: |
|  [Fundamentals](https://github.com/Yugabyte/yugabyte-db/wiki/YSQL-Tutorial:-Fundamentals)| SELECT data from one column | :heavy_check_mark: |  |
|   | SELECT data from multiple columns | :heavy_check_mark:  | |
|   | SELECT data from all columns | :heavy_check_mark: |  |
|   | SELECT with an expression | :heavy_check_mark: |  |
|   | SELECT with an expression, but without a FROM clause | :heavy_check_mark: |  |
|  |  SELECT with a column alias | :heavy_check_mark: | |  
|| SELECT with a table alias  | :heavy_check_mark: | |  
||  SELECT with an ascending ORDER BY | :heavy_check_mark: | |  
|| SELECT with an descending ORDER BY  | :heavy_check_mark: | |  
||  SELECT with a ascending and descending ORDER BYs | :heavy_check_mark: | | 
||  SELECT with DISTINCT on one column | :heavy_check_mark:  | | 
||  SELECT with DISTINCT including multiple columns | :heavy_check_mark: | | 
|| SELECT with DISTINCT ON expression  | :heavy_check_mark: | |  
||  WHERE clause with an equal = operator |  | | 
||  WHERE clause with an AND operator |  | | 
|| WHERE clause with an OR operator  |  | | 
|| WHERE clause with an IN operator  |  | | 
||  WHERE clause with a LIKE operator |  | | 
||  WHERE clause with a BETWEEN operator |  | |  
||  WHERE clause with a not equal &lt;&gt; operator |  | | 
|| SELECT with a LIMIT clause |  | | 
||SELECT with LIMIT and OFFSET clauses|  | |  |
|| SELECT with LIMIT and ORDER BY clauses  |  | | 
||  SELECT with FETCH and ORDER BY clauses |  | | 
||  SELECT with FETCH, OFFSET and ORDER BY clauses |  | | 
||  SELECT with an IN clause |  | | 
||  SELECT with a NOT IN clause |  | | 
||  SELECT with an IN clause in a subquery using CAST |  | | 
||  SELECT with BETWEEN |  | | 
||  SELECT with NOT BETWEEN |  | | 
||  SELECT with a LIKE operator |  | |
||  SELECT with a LIKE operator using  % and _  |  | | 
||  SELECT with a NOT LIKE operator |  | | 
||  SELECT with an ILIKE operator |  | | 
|| SELECT with a IS NULL operator  |  | | 
||  SELECT with a IS NOT NULL operator |  | | 
||  SELECT with an INNER JOIN |  | | 
||  SELECT with a LEFT OUTER JOIN |  | |
||  SELECT with a LEFT OUTER JOIN with rows only from left table |  | | 
||  SELECT with RIGHT OUTER JOIN | :heavy_check_mark: | | 
||  SELECT with FULL RIGHT OUTER JOIN |  | | 
||  SELECT with FULL OUTER JOIN |  | | 
||  SELECT with FULL OUTER JOIN with only unque rows in both tables|  | |