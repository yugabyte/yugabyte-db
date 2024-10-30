Orafce Documentation
===

Orafce - Oracle's compatibility functions and packages
---

This documentation describes the environment settings and functionality offered for features that are compatible with Oracle databases.


Chapter 1 Overview
---

Features compatible with Oracle databases are provided. 
These features enable you to easily migrate to PostgreSQL and reduce the costs of reconfiguring applications.

The table below lists features compatible with Oracle databases.



### 1.1 Features compatible with Oracle databases

**Data type**

|Item|Overview|
|:---|:---|
|VARCHAR2|Variable-length character data type|
|NVARCHAR2|Variable-length national character data type|
|DATE|Data type that stores date and time|

**SQL Queries**

|Item|Overview|
|:---|:---|
|DUAL table|Table provided by the system|


**SQL Functions**

 - Mathematical functions

|Item|Overview|
|:---|:---|
|BITAND|Performs a bitwise AND operation|
|COSH|Calculates the hyperbolic cosine of a number|
|SINH|Calculates the hyperbolic sine of a number|
|TANH|Calculates the hyperbolic tangent of a number|


 - String functions

|Item|Overview|
|:---|:---|
|INSTR|Returns the position of a substring in a string|
|LENGTH|Returns the length of a string in number of characters|
|LENGTHB|Returns the length of a string in number of bytes|
|LPAD|Left-pads a string to a specified length with a sequence of characters|
|LTRIM|Removes the specified characters from the beginning of a string|
|NLSSORT|Returns a byte string used to sort strings in linguistic sort sequence based on locale|
|REGEXP_COUNT|searches a string for a regular expression, and returns a count of the matches|
|REGEXP_INSTR|returns the beginning or ending position within the string where the match for a pattern was located|
|REGEXP_LIKE|condition in the WHERE clause of a query, causing the query to return rows that match the given pattern|
|REGEXP_SUBSTR|returns the string that matches the pattern specified in the call to the function|
|REGEXP_REPLACE|replace substring(s) matching a POSIX regular expression|
|RPAD|Right-pads a string to a specified length with a sequence of characters|
|RTRIM|Removes the specified characters from the end of a string|
|SUBSTR|Extracts part of a string using characters to specify position and length|
|SUBSTRB|Extracts part of a string using bytes to specify position and length|


 - Date/time functions

|Item|Overview|
|:---|:---|
|ADD_MONTHS|Adds months to a date|
|DBTIMEZONE|Returns the value of the database time zone|
|LAST_DAY|Returns the last day of the month in which the specified date falls|
|MONTHS_BETWEEN|Returns the number of months between two dates|
|NEXT_DAY|Returns the date of the first instance of a particular day of the week that follows the specified date|
|ROUND|Rounds a date|
|SESSIONTIMEZONE|Returns the time zone of the session|
|SYSDATE|Returns the system date|
|TRUNC|Truncates a date|


 - Data type formatting functions

|Item|Overview|
|:---|:---|
|TO_CHAR|Converts a value to a string|
|TO_DATE|Converts a string to a date in accordance with the specified format|
|TO_MULTI_BYTE|Converts a single-byte string to a multibyte string|
|TO_NUMBER|Converts a value to a number in accordance with the specified format|
|TO_SINGLE_BYTE|Converts a multibyte string to a single-byte string|


 - Conditional expressions

|Item|Overview|
|:---|:---|
|DECODE|Compares values, and if they match, returns a corresponding value|
|GREATEST|Returns the greatest of the list of one or more expressions|
|LEAST|Returns the least of the list of one or more expressions|
|LNNVL|Evaluates if a value is false or unknown|
|NANVL|Returns a substitute value when a value is not a number (NaN)|
|NVL|Returns a substitute value when a value is NULL|
|NVL2|Returns a substitute value based on whether a value is NULL or not NULL|


 - Aggregate functions

|Item|Overview|
|:---|:---|
|LISTAGG|Returns a concatenated, delimited list of string values|
|MEDIAN|Calculates the median of a set of values|

 - Functions that return internal information

|Item|Overview|
|:---|:---|
|DUMP|Returns internal information of a value|



**SQL Operators**

|Item|Overview|
|:---|:---|
|Datetime operator|Datetime operator for the DATE type|


**Packages**

|Item|Overview|
|:---|:---|
|DBMS_ALERT|Sends alerts to multiple sessions|
|DBMS_ASSERT|Validates the properties of an input value|
|DBMS_OUTPUT|Sends messages to clients|
|DBMS_PIPE|Creates a pipe for inter-session communication|
|DBMS_RANDOM|Generates random numbers|
|DBMS_UTILITY|Provides various utilities|
|UTL_FILE|Enables text file operations|



