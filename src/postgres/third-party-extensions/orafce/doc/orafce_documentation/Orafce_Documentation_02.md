Chapter 2 Notes on Using orafce
---

Orafce is defined as user-defined functions in the "public" schema created by default when database clusters are created, so they can be available for all users without the need for special settings.
For this reason, ensure that "public" (without the double quotation marks) is included in the list of schema search paths specified in the search_path parameter.

The following features provided by orafce are implemented in PostgreSQL and orafce using different external specifications. In the default configuration of PostgreSQL, the standard features of PostgreSQL take precedence.



**Features implemented in PostgreSQL and orafce using different external specifications**

 - Data type

|Item|Standard feature of PostgreSQL|Compatibility feature added by orafce|
|:---|:---|:---|
|DATE|Stores date only.|Stores date and time.|


 - Function

|Item|Standard feature of PostgreSQL|Compatibility feature added by orafce|
|:---|:---|:---|
|LENGTH|If the string is CHAR type, trailing spaces are not included in the length.|If the string is CHAR type, trailing spaces are included in the length.|
|SUBSTR|If 0 or a negative value is specified for the start position, simply subtracting 1 from the start position, the position will be shifted to the left, from where extraction will start.| - If 0 is specified for the start position, extraction will start from the beginning of the string. <br>  - If a negative value is specified for the start position, extraction will start from the position counted from the end of the string.|
|LPAD <br> RPAD| - If the string is CHAR type, trailing spaces are removed and then the value is padded.<br> - The result length is handled as a number of characters.| - If the string is CHAR type, the value is padded without removing trailing spaces. <br>  - The result length is based on the width of the displayed string. Therefore, fullwidth characters are handled using a width of 2, and halfwidth characters are handled using a width of 1.|
|LTRIM <br> RTRIM <br> BTRIM (*1)|If the string is CHAR type, trailing spaces are removed and then the value is removed.|If the string is CHAR type, the value is removed without removing trailing spaces.|
|TO_DATE|The data type of the return value is DATE.|The data type of the return value is TIMESTAMP.|


*1: BTRIM does not exist for Oracle databases, however, an external specification different to PostgreSQL is implemented in orafce to align with the behavior of the TRIM functions.

Also, the following features cannot be used in the default configuration of PostgreSQL.




**Features that cannot be used in the default configuration of PostgreSQL**


 - Function

|Feature|
|:---|
|SYSDATE|
|DBTIMEZONE|
|SESSIONTIMEZONE|
|TO_CHAR (date/time value)|

 - Operator

|Feature|
|:---|
|Datetime operator|

To use these features, set "oracle" and "pg_catalog" in the "search_path" parameter of postgresql.conf. You must specify "oracle" before "pg_catalog" when doing this.

~~~
search_path = '"$user", public, oracle, pg_catalog'
~~~

**Information**

----

 - The search_path parameter specifies the order in which schemas are searched. Each feature compatible with Oracle databases is defined in the oracle schema.
 - It is recommended to set search_path in postgresql.conf. In this case, it will be effective for each instance.
 - The configuration of search_path can be done at the user level or at the database level. Setting examples are shown below.
 - If the standard features of PostgreSQL take precedence, and features that cannot be used with the default configuration of PostgreSQL are not required, it is not necessary to change the settings of search_path.

 - Example of setting at the user level 
    - This can be set by executing an SQL command. In this example, user1 is used as the username.

~~~
ALTER USER user1 SET search_path = "$user",public,oracle,pg_catalog;
~~~

 - Example of setting at the database level
    - This can be set by executing an SQL command. In this example, db1 is used as the database name.<br> You must specify "oracle" before "pg_catalog".

~~~
ALTER DATABASE db1 SET search_path = "$user",public,oracle,pg_catalog;
~~~



----


**See**

----

 - Refer to "Server Administration" > "Client Connection Defaults" > "Statement Behavior" in the PostgreSQL Documentation for information on search_path.
 - Refer to "Reference" > "SQL Commands" in the PostgreSQL Documentation for information on ALTER USER and ALTER DATABASE.

----

