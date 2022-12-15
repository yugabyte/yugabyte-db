---
title: Strings and text in YSQL
headerTitle: Strings and text
linkTitle: 8. Strings and text
description: Learn how to work with string and text data types in YSQL.
menu:
  v2.14:
    identifier: strings-and-text-1-ysql
    parent: learn
    weight: 570
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="{{< relref "./strings-and-text-ysql.md" >}}" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>

  <li >
    <a href="{{< relref "./strings-and-text-ycql.md" >}}" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>

</ul>

## Introduction

Strings, character data types, or text are part of every conceivable system. Manipulating and outputting text is a very important topic that is required for many different types of systems that you work with. This section provides an overview of the YugabyteDB SQL API's extensive text capabilities.

The examples use the [Retail Analytics sample dataset](../../../sample-data/retail-analytics/).

## Character data types

With PostgreSQL, the use of different character data types has a historical aspect. YugabyteDB — being a more recent implementation — has no such history. Consider keeping your use of character data types simple, ideally just 'text', or 'varchar(n)' if you require a restricted length. Using text and then verifying the length of a character string allows you to develop your own approach to managing this scenario, rather than encountering errors by exceeding some arbitrary length.

If you use char(n), character(n), or varchar(n), then the limitation will be the number you assign, which cannot exceed 10,485,760. For unlimited length, use a character data type without a length description, such as 'text'. However, if you have specific requirements to ignore trailing spaces, then you may wish to consider using char(n).

For more information on character data types, refer to [Data types](/preview/api/ysql/datatypes/). Note that YugabyteDB implements the data type aliases and that is what is used here.

The following example shows a few ways to work with different data types:

```sh
./bin/ysqlsh
```

```output
ysqlsh (11.2)
Type "help" for help.
```

```sql
yugabyte=# create table text_columns(a_text text, a_varchar varchar, a_char char, b_varchar varchar(10), b_char char(10));
```

```output
CREATE TABLE
```

```sql
yugabyte=# insert into text_columns values('abc ', 'abc ', 'abc ', 'abc ', 'abc ');
```

```output
ERROR:  value too long for type character(1)
```

```sql
yugabyte=# insert into text_columns values('abc ', 'abc ', 'a', 'abc ', 'abc ');
```

```output
INSERT 0 1
```

```sql
yugabyte=# select * from text_columns
           where a_text like 'ab__' and a_varchar like 'ab__'
           and b_varchar like 'ab__';
```

```output
 a_text | a_varchar | a_char | b_varchar |   b_char
--------+-----------+--------+-----------+------------
 abc    | abc       | a      | abc       | abc
```

```sql
yugabyte=# select * from text_columns
           where a_text like 'ab__' and a_varchar like 'ab__'
           and b_varchar like 'ab__' and b_char like 'ab__';
```

```output
 a_text | a_varchar | a_char | b_varchar | b_char
--------+-----------+--------+-----------+--------
(0 rows)
```

```sql
yugabyte=# select length(a_text) as a_text, length(a_varchar) as a_varchar, length(a_char) as a_char,
           length(b_varchar) as b_varchar, length(b_char) as b_char
           from text_columns;
```

```output
 a_text | a_varchar | a_char | b_varchar | b_char
--------+-----------+--------+-----------+--------
      4 |         4 |      1 |         4 |      3
```

Notice that the column `b_char` does not contain a trailing space and this could impact your SQL. In addition, if you specify a maximum length on the column definition, the SQL can also generate errors, so you have to either manually truncate your input values or introduce error handling.

### Casting

When you are working with text that has been entered by users through an application, ensure that YugabyteDB understands that it is working with a text input. All values should be cast unless they can be trusted due to other validation measures that have already occurred.

The following example shows the impacts of casting:

```sql
yugabyte=# select cast(123 AS TEXT), cast('123' AS TEXT), 123::text, '123'::text;
```

```output
 text | text | text | text
------+------+------+------
 123  | 123  | 123  | 123
```

```sql
yugabyte=# select tablename, hasindexes AS nocast, hasindexes::text AS casted
  from pg_catalog.pg_tables
  where tablename in('pg_default_acl', 'sql_features');
```

```output
   tablename    | nocast | casted
----------------+--------+--------
 pg_default_acl | t      | true
 sql_features   | f      | false
```

T he column 'hasindexes' is a `Boolean` data type and by casting it to `text`, you receive a text result of `true` or `false`.

## Manipulating text

Many functions can be applied to text. In the examples that follow, the functions are classified into logical groupings - in many cases the capability of the functions overlap and personal choice determines how you approach solving the problem.

The focus here is to quickly show how each of the functions can be used, along with some examples.

The example assumes that you have created and connected to the `yb_demo` database with the [Retail Analytics sample dataset](../../../sample-data/retail-analytics/).

### Altering the appearance of text

```sql
yb_demo =# select lower('hELLO world') AS LOWER,
  upper('hELLO world') AS UPPER,
  initcap('hELLO world') AS INITCAP;
```

```output
    lower    |    upper    |   initcap
-------------+-------------+-------------
 hello world | HELLO WORLD | Hello World
```

```sql
yb_demo =# select quote_ident('ok') AS EASY, quote_ident('I am OK') AS QUOTED, quote_ident('I''m not OK') AS DOUBLE_QUOTED, quote_ident('') AS EMPTY_STR, quote_ident(null) AS NULL_QUOTED;
```

```output
 easy |  quoted   | double_quoted | empty_str | null_quoted
------+-----------+---------------+-----------+-------------
 ok   | "I am OK" | "I'm not OK"  | ""        |
```

```sql
yb_demo =# select quote_literal('ok') AS EASY, quote_literal('I am OK') AS QUOTED, quote_literal('I''m not OK') AS DOUBLE_QUOTED, quote_literal('') AS EMPTY_STR, quote_literal(null) AS NULL_QUOTED;
```

```output
 easy |  quoted   | double_quoted | empty_str | null_quoted
------+-----------+---------------+-----------+-------------
 'ok' | 'I am OK' | 'I''m not OK' | ''        |
```

```sql
yb_demo =# select quote_nullable('ok') AS EASY, quote_nullable('I am OK') AS QUOTED, quote_nullable('I''m not OK') AS DOUBLE_QUOTED, quote_nullable('') AS EMPTY_STR, quote_nullable(null) AS NULL_QUOTED;
```

```output
easy |  quoted   | double_quoted | empty_str | null_quoted
------+-----------+---------------+-----------+-------------
 'ok' | 'I am OK' | 'I''m not OK' | ''        | NULL
```

Use `quote_ident` to parse identifiers in SQL like column names and `quote_nullable` as a string literal that may also be a null.

### Parsing raw text

You can use "dollar sign quoting" to parse raw text — any text enclosed in dollar sign (`$`) quotations are treated as a raw literal. The starting and ending markers do not need to be identical, but must start and end with a dollar sign. Consider the following examples:

```sql
yb_demo=# select $$%&*$&$%7'\67458\''""""';;'\//\/\/\""'/'''''"""""'''''''''$$;
```

```output
                         ?column?
-----------------------------------------------------------
 %&*$&$%7'\67458\''""""';;'\//\/\/\""'/'''''"""""'''''''''
```

```sql
yb_demo=# select $__unique_$           Lots of space
yb_demo=#                    and multi-line too       $__unique_$;
```

```output
                   ?column?
----------------------------------------------
            Lots of space                    +
                    and multi-line too
```

```sql
yb_demo=# select $$first$$ AS "F1", $$second$$ AS "F2";
```

```output
  F1   |   F2
-------+--------
 first | second
```

### Padding and trimming

Some values need to be padded for formatting purposes, and `lpad()` and `rpad()` ('left pad' and 'right pad', respectively) are meant for this purpose. They are normally used with spaces, but you can pad using anything, including more than a single character. For example, you can pad with underscores (`_`) or spaced dots `. . .`. You do not specify how much to pad, but the maximum length to pad. Therefore, if your value is already as long as your maximum length, then no padding is required. Note that this can cause truncation if your field is longer than the maximum length specified.

The reverse of padding is trimming, which removes spaces if found. The following examples use padding and trimming to achieve the results required:

```sql
yb_demo=# select name, lpad(name, 10), rpad(name, 15) from users order by name limit 5;
```

```output
       name        |    lpad    |      rpad
-------------------+------------+-----------------
 Aaron Hand        | Aaron Hand | Aaron Hand
 Abbey Satterfield | Abbey Satt | Abbey Satterfie
 Abbie Parisian    | Abbie Pari | Abbie Parisian
 Abbie Ryan        | Abbie Ryan | Abbie Ryan
 Abby Larkin       | Abby Larki | Abby Larkin
```

```sql
yb_demo=# select name, lpad(name, 20), rpad(name, 20) from users order by name limit 5;
```

```output
       name        |         lpad         |         rpad
-------------------+----------------------+----------------------
 Aaron Hand        |           Aaron Hand | Aaron Hand
 Abbey Satterfield |    Abbey Satterfield | Abbey Satterfield
 Abbie Parisian    |       Abbie Parisian | Abbie Parisian
 Abbie Ryan        |           Abbie Ryan | Abbie Ryan
 Abby Larkin       |          Abby Larkin | Abby Larkin
```

```sql
yb_demo=# select name, lpad(name, 20, '. '), rpad(name, 20, '.') from users order by name limit 5;
```

```output
       name        |         lpad         |         rpad
-------------------+----------------------+----------------------
 Aaron Hand        | . . . . . Aaron Hand | Aaron Hand..........
 Abbey Satterfield | . .Abbey Satterfield | Abbey Satterfield...
 Abbie Parisian    | . . . Abbie Parisian | Abbie Parisian......
 Abbie Ryan        | . . . . . Abbie Ryan | Abbie Ryan..........
 Abby Larkin       | . . . . .Abby Larkin | Abby Larkin.........
```

```sql
yb_demo=# select repeat(' ', ((x.maxlen-length(u.name))/2)::int) || rpad(u.name, x.maxlen) AS "cname"
          from users u,
          (select max(length(a.name))::int AS maxlen from users a) AS x;
```

```output
            cname
------------------------------
      Stewart Marks
      Regan Corkery
    Domenic Daugherty
    Winfield Donnelly
    Theresa Kertzmann
    Terrence Emmerich
      Hudson Jacobi
      Aidan Hagenes
    Virgil Schowalter
      Rahul Kreiger
    Wilhelmine Erdman
      Elwin Okuneva
  Maximillian Dickinson
      Lucie Cormier
  Alexandrine Rosenbaum
    Jayne Breitenberg
  Alexandria Schowalter
 Augustine Runolfsdottir
    Mathilde Weissnat
      Theresa Grant
 ...
```

```sql
yb_demo=# select x.RawDay, length(x.RawDay) AS RawLen, x.TrimDay, length(x.TrimDay) AS TrimLen,
          x.LTrimDay, length(x.LTrimDay) AS LTrimLen, x.RTrimDay, length(x.RTrimDay) AS RTrimLen
          from (select to_char(generate_series, 'Day') AS RawDay,
                trim(to_char(generate_series, 'Day')) AS TrimDay,
                ltrim(to_char(generate_series, 'Day')) AS LTrimDay,
                rtrim(to_char(generate_series, 'Day')) AS RTrimDay
                from generate_series(current_date, current_date+6, '1 day')) AS x;
```

```output
  rawday   | rawlen |  trimday  | trimlen | ltrimday  | ltrimlen | rtrimday  | rtrimlen
-----------+--------+-----------+---------+-----------+----------+-----------+----------
 Wednesday |      9 | Wednesday |       9 | Wednesday |        9 | Wednesday |        9
 Thursday  |      9 | Thursday  |       8 | Thursday  |        9 | Thursday  |        8
 Friday    |      9 | Friday    |       6 | Friday    |        9 | Friday    |        6
 Saturday  |      9 | Saturday  |       8 | Saturday  |        9 | Saturday  |        8
 Sunday    |      9 | Sunday    |       6 | Sunday    |        9 | Sunday    |        6
 Monday    |      9 | Monday    |       6 | Monday    |        9 | Monday    |        6
 Tuesday   |      9 | Tuesday   |       7 | Tuesday   |        9 | Tuesday   |        7
```

The preceding example shows how you can center text and the trim example shows the impacts of the different trims on a value that is padded. Note that the 'Day' value is right-padded to 9 characters, which is why a left-trim has no impact on the field length at all; only the right-trim or a 'full' trim will remove spaces.

### Escaping

You can also state that a text value is 'escaped' by prefixing with an 'e' or 'E'. For example:

```sql
yb_demo=# select E'I''ve told YugabyteDB that this is an escaped string\n\tso I can specify escapes safely' as escaped_text;
```

```output
                   escaped_text
---------------------------------------------------
 I've told YugabyteDB that this is an escaped string+
         so I can specify escapes safely
```

```sql
yb_demo=# select E'a\\b/c\u00B6' as escaped_txt, 'a\\b/c\u00B6' as raw_txt;
```

```output
 escaped_txt |   raw_txt
-------------+--------------
 a\b/c¶     | a\\b/c\u00B6
```

{{< note title="Note" >}}
`\n` refers to a new line, and `\t` is a tab, hence the formatted result.
{{< /note >}}

### Encoding and converting text

YugabyteDB also has `DECODE` and `ENCODE` for decoding and encoding from, or to, binary data. It caters for 'base64', 'hex', and 'escape' representations. Decode gives the output in `BYTEA` data type. Additionally, you can use the `TO_HEX` command to convert an ASCII number to its digital representation.

### Joining strings

You can concatenate strings of text in several different ways. For robustness, you should ensure that everything being passed is interpreted as text (by casting) so that unexpected results do not appear in edge cases. The following examples show that YugabyteDB is lenient in passing in variables, but you should implement more robust casting for proper treatment of strings:

```sql
yb_demo=# select 'one' || '-' || 2 || '-one' AS "121";
```

```output
    121
-----------
 one-2-one
```

```sql
yb_demo=# select 2 || '-one-one' AS "211";
```

```output
    211
-----------
 2-one-one
```

```sql
yb_demo=# select 1 || '-one' || repeat('-two', 2) AS "1122";
```

```output
     1122
---------------
 1-one-two-two
```

```sql
yb_demo=# select 1::text || 2::text || 3::text AS "123";
```

```output
 123
-----
 123
```

```sql
yb_demo=# select 1 || 2 || 3 AS "123";
```

```output
ERROR:  operator does not exist: integer || integer
LINE 1: select 1 || 2 || 3 AS "123";
                 ^
HINT:  No operator matches the given name and argument types. You might need to add explicit type casts.
```

```sql
yb_demo=# select concat(1,2,3) AS "123";
```

```output
 123
-----
 123
```

```sql
yb_demo=# select concat_ws(':', 1,2,3) AS "123 WS";
```

```output
 123 WS
--------
 1:2:3
(1 row)
```

```sql
yb_demo =# select left(vendor,1) AS V, string_agg(distinct(category), ', ' ORDER BY category) AS CATEGORIES
  from products group by left(vendor,1) order by 1;
```

```output
 v |            categories
---+----------------------------------
 A | Doohickey, Gadget, Gizmo
 B | Doohickey, Gadget, Gizmo, Widget
 C | Doohickey, Gadget, Gizmo, Widget
 D | Gadget, Gizmo, Widget
 E | Gadget, Gizmo, Widget
 F | Doohickey, Gadget, Gizmo, Widget
 G | Doohickey, Gadget, Widget
 H | Doohickey, Gadget, Gizmo, Widget
 I | Gizmo, Widget
 J | Doohickey, Gadget, Gizmo, Widget
 K | Doohickey, Gadget, Gizmo, Widget
 L | Doohickey, Gadget, Gizmo, Widget
 M | Doohickey, Gadget, Gizmo, Widget
 N | Doohickey, Gadget, Widget
 O | Doohickey, Gadget, Gizmo, Widget
 P | Doohickey, Gadget, Gizmo, Widget
 Q | Doohickey
 R | Doohickey, Gadget, Gizmo, Widget
 S | Doohickey, Gadget, Gizmo, Widget
 T | Gizmo, Widget
 U | Gadget
 V | Doohickey, Widget
 W | Doohickey, Gadget, Gizmo, Widget
 Z | Gizmo
```

The preceding example uses the `LEFT` function, but the `string_agg` function is best used by an input of a series or a set of data as done in SQL rows. The example shows how the aggregated string has its own order by compared to the outer SQL which is the vendors being classified A-Z.

The `REVERSE` function reverses the contents of text as shown in the following example:

```sql
yb_demo=# select reverse(to_char(current_date, 'DD-MON-YYYY'));
```

```output
   reverse
-------------
 9102-LUJ-92
```

### Parsing user input

To minimise the impact of unexpected data that is typical of a SQL injection attack, you can use the `FORMAT` function to parse user input as parameters to a SQL statement. The most popular method is to use the `EXECUTE` command in a procedure as this is not available at the YSQL command prompt, only in the YSQL PL/pgSQL environment. The `FORMAT` command is used to finalise the complete SQL statement which is passed to `EXECUTE` to run. As you are not simulating YSQL PL/pgSQL here, the following example illustrates how to use the `FORMAT` function only:

```sql
yb_demo=# select format('Hello %s, today''s date is %s', 'Jono', to_char(current_date, 'DD-MON-YYYY'), 'discarded');
```

```output
                 format
-----------------------------------------
 Hello Jono, today's date is 29-JUL-2019
```

```sql
yb_demo=# select format('On this day, %2$s, %1$s was here', 'Jono', to_char(current_date, 'DD-MON-YYYY'));
```

```output
                 format
-----------------------------------------
 On this day, 29-JUL-2019, Jono was here
```

```sql
yb_demo=# select format('SELECT %2$I, %3$I from %1$I where name = %4$L', 'users', 'birth_date', 'email', 'Brody O''Reilly');
```

```output
                               format
--------------------------------------------------------------------
 SELECT birth_date, email from users where name = 'Brody O''Reilly'
```

### Substituting text

Substituting text with other text can be a complex task, as you need to fully understand the scope of the data that the functions can be subject to. A common occurrence is failure due to an unexpected value being passed through, like `NULL`, an empty string `''`, or a value that YugabyteDB would interpret as a different data type like `true` or `3`.

The treatment of nulls in mathematical operations is often problematic, as are string joins as joining a null to a value results in a null. Coalescing the inputs avoids these issues as shown in the following examples:

```sql
yb_demo=# select trunc(avg(coalesce(discount,0))::numeric,3) AS "COALESCED", trunc(avg(discount)::numeric,3) AS "RAW" from orders;
```

```output
 COALESCED |  RAW
-----------+-------
     0.530 | 5.195
```

```sql
yb_demo=# select 'Hello ' || null AS GREETING, 'Goodbye ' || coalesce(null, 'Valued Customer') AS GOODBYE;
```

```output
 greeting |         goodbye
----------+-------------------------
          | Goodbye Valued Customer
```

The preceding example shows how substituting when null can have a significant impact on the results you achieve, or even the behaviour of your application.

The following example demonstrates ways to change existing text using other text.

```sql
yb_demo=# select overlay(password placing 'XXXXXXXXXXXXXXX' from 1 for length(password)) AS SCRAMBLED from users limit 5;
```

```output
    scrambled
-----------------
 XXXXXXXXXXXXXXX
 XXXXXXXXXXXXXXX
 XXXXXXXXXXXXXXX
 XXXXXXXXXXXXXXX
 XXXXXXXXXXXXXXX
```

```sql
yb_demo=# select regexp_replace('Hi my number is +999 9996-1234','[[:alpha:]]','','g');
```

```output
   regexp_replace
--------------------
     +999 9996-1234
```

```sql
yb_demo=# select 'I think I can hear an ' || repeat('echo.. ', 3) AS CLICHE;
```

```output
                   cliche
---------------------------------------------
 I think I can hear an echo.. echo.. echo..
```

```sql
yb_demo=# select replace('Gees I love Windows', 'Windows', 'Linux') AS OBVIOUS;
```

```output
      obvious
-------------------
 Gees I love Linux
```

The `REGEXP_REPLACE` function along with the other REGEX functions require an entire chapter to themselves given the sophistication that can be achieved. The preceding example strips out all characters of the alphabet and replaces them with an empty string. The 'g' flag is 'global', and results in the replacement occurring throughout the entire string; without the 'g' flag the replace stops after the first substitution. Note that the result contains spaces which is why it appears odd. You might think that this example shows an extraction of non-alphabetical characters, but it is just replacing them with an empty string.

### Extracting text

There are several ways to extract text from text; in some cases it might be part of 'cleaning' the text. (Removing leading or trailing spaces is covered by the trim functions shown in a preceding section.) The remaining functions here show how parts of text can be manipulated.

```sql
yb_demo=# select left('123456', 3);
```

```output
 left
------
 123
```

```sql
yb_demo=# select right('123456', 3);
```

```output
 right
-------
 456
```

```sql
yb_demo=# select substr('123456', 3);
```

```output
 substr
--------
 3456
```

```sql
yb_demo=# select substr('123456', 3, 2);
```

```output
 substr
--------
 34
```

```sql
yb_demo=# select substr('123456', position('4' in '123456')+1, 2);
```

```output
 substr
--------
 56
```

```sql
yb_demo=# select substring('123456', position('4' in '123456')+1, 2);
```

```output
 substring
-----------
 56
```

```sql
yb_demo=# select replace(substr(email, position('@' in email)+1, (length(email)
            -position('.' in substr(email, position('@' in email)+1)))), '.com', '') AS "Domain", count(*)
          from users
          group by 1;
```

```output
 Domain  | count
---------+-------
 hotmail |   813
 yahoo   |   838
 gmail   |   849

```

The command `SUBSTRING` has overloaded equivalents that accept POSIX expressions. The preceding example shows a basic use of `SUBSTRING` (which can also be used as `SUBSTR`). It is recommended to only use the full `SUBSTRING` command when using POSIX.

### Regular expressions

A full description of regular expressions requires its own comprehensive documentation that is not covered here. The following example illustrates their use:

```sql
yb_demo=# select name as Fullname, regexp_match(name, '(.*)(\s+)(.*)') AS "REGEXED Name",
          (regexp_match(name, '(.*)(\s+)(.*)'))[1] AS "First Name",
          (regexp_match(name, '(.*)(\s+)(.*)'))[3] AS "Last Name"
          from users limit 5;
```

```output
    fullname    |     REGEXED Name     | First Name | Last Name
----------------+----------------------+------------+-----------
 Jacinthe Rowe  | {Jacinthe," ",Rowe}  | Jacinthe   | Rowe
 Walter Mueller | {Walter," ",Mueller} | Walter     | Mueller
 Fatima Murphy  | {Fatima," ",Murphy}  | Fatima     | Murphy
 Paxton Mayer   | {Paxton," ",Mayer}   | Paxton     | Mayer
 Mellie Wolf    | {Mellie," ",Wolf}    | Mellie     | Wolf
```

In the preceding example, you are asking the 'name' column to be delimited by the existence of a space (`\s`) and then reporting the first and third set of text reported by the match. The regular expression returns a text array, not a text value, and thus you have to reference the array index to access the value as text. Note that this SQL would be very vulnerable to errors caused by data entry, including a middle name or missing either a first or last name would cause errors.

Now, let's look at some manipulation and splitting of text so that you can process it in pieces. The following example uses a sample extract from a bank file that is used for processing payments. This example could apply if the entire file was uploaded as a single text entry into a table and you select it and then process it.

```sql
yb_demo=# create table bank_payments(bank_file text);
```

```output
CREATE TABLE
```

```sql
yb_demo=# insert into bank_payments values($$"CMGB","1.0","95012141352105","999999","30128193018492","20","","GBP","B","Beneficiary name18","Txt on senders acc","Txt for credit acc","","","","","","909170/1","AB"
"CMGB","1.0","95012141352105","999999","95012113864863","10.00","","GBP","B","Beneficiary name18","Txt on senders acc","Txt for credit acc","","","","","Remitters name  18","Tech ref for automatic processing5","AT","/t.x",
"CMGB","1.0","95012141352105","","30128193018492","21","","GBP","C","Beneficiary name18","Txt on senders acc","","Txt for credit acc","","","","","909175/0","AB"
"CMGB","1.0","95012141352105","","30128193018492","22","","GBP","I","Beneficiary name18","Txt on senders acc","text","","","","","","909175/1","AB"
"CMGB","1.0","95012141352105","","30128193018492","23","","GBP","F","Beneficiary name18","Txt on senders acc","Txt for credit acc","","","","","","909171/0","AB"$$);
```

```output
INSERT 0 1
```

```sql
yb_demo=# select regexp_split_to_table(bank_file, chr(10)) from bank_payments;
```

```output
                                                                                                     regexp_split_to_table
--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 "CMGB","1.0","95012141352105","999999","30128193018492","20","","GBP","B","Beneficiary name18","Txt on senders acc","Txt for credit acc","","","","","","909170/1","AB"
 "CMGB","1.0","95012141352105","999999","95012113864863","10.00","","GBP","B","Beneficiary name18","Txt on senders acc","Txt for credit acc","","","","","Remitters name  18","Tech ref for automatic processing5","AT","/t.x",
 "CMGB","1.0","95012141352105","","30128193018492","21","","GBP","C","Beneficiary name18","Txt on senders acc","","Txt for credit acc","","","","","909175/0","AB"
 "CMGB","1.0","95012141352105","","30128193018492","22","","GBP","I","Beneficiary name18","Txt on senders acc","text","","","","","","909175/1","AB"
 "CMGB","1.0","95012141352105","","30128193018492","23","","GBP","F","Beneficiary name18","Txt on senders acc","Txt for credit acc","","","","","","909171/0","AB"
```

```sql
yb_demo=# select split_part(f.line, ',' , 8) AS "currency",
                 split_part(f.line, ',' , 5) AS "Account"
                 from (select regexp_split_to_table(bank_file, chr(10)) AS "line" from bank_payments) AS f;
```

```output
 currency |     Account
----------+------------------
 "GBP"    | "30128193018492"
 "GBP"    | "95012113864863"
 "GBP"    | "30128193018492"
 "GBP"    | "30128193018492"
 "GBP"    | "30128193018492"
```

_Remember to drop the table 'bank_payments' if it is no longer required._

```sql
yb_demo=# select reverse(translate(replace(lower(i.input), ' ', ''),
                         'abcdefghijklmnopqrstuvwxyz',
                         'A8Cd349h172!mN0pQr$TuVw*yZ')) AS "simplePWD"
          from (select 'type a word here' AS "input") AS i;
```

```output
   simplePWD
---------------
 3r3hdr0wA3pyT
```

The preceding `TRANSLATE` command replaces multiple different characters in a single command, which can be useful. In the example, the 'a' is replaced with a 'A', and 'b' is replaced with the number '8', and so forth.

## Obtaining information about text

Rather than format or change the contents of text, you often might want to understand particular attributes of the text. The following examples use commands to return information about the text:

```sql
yb_demo=# select x.c AS CHAR, ascii(x.c) AS ASCII
          from (select regexp_split_to_table(i.input, '') AS "c"
                from (select 'hello' AS input) AS i) AS x;
```

```output
 char | ascii
------+-------
 h    |   104
 e    |   101
 l    |   108
 l    |   108
 o    |   111
```

```sql
yb_demo=# select bit_length('hello'), char_length('hello'), octet_length('hello');
```

```output
 bit_length | char_length | octet_length
------------+-------------+--------------
         40 |           5 |            5
```

```sql
yb_demo=# select array_agg(chr(ascii(x.c))) AS "CHAR"
          from (select regexp_split_to_table(i.input, '') AS "c"
                from (select 'hello' AS input) AS i) AS x;
```

```output
    CHAR
-------------
 {h,e,l,l,o}
```

```sql
yb_demo=# select avg(length(name))::int AS AVG_LENGTH from users;
```

```output
 avg_length
------------
         14
```

```sql
yb_demo=# select name from users
          where position('T' in name) > 2
          and position('p' in name) = length(name)
          order by name;
```

```output
      name
-----------------
 Cory Tromp
 Demario Tromp
 Demetris Tromp
 Deon Tromp
 Emelia Tromp
 Ivah Tromp
 Jany Torp
 Jared Tromp
 Judd Tromp
 Larue Torp
 Magdalen Torp
 Margarita Tromp
 Marjolaine Torp
 Patrick Torp
 Porter Tromp
 Rebeka Tromp
```

```sql
yb_demo=# select name, position('ar' in name) AS posn, strpos(name, 'ar') as strpos
          from users
          where strpos(name, 'ark') > 0
          order by name desc limit 10;
```

```output
      name      | posn | strpos
----------------+------+--------
 Yasmin Stark   |   10 |     10
 Veronica Stark |   12 |     12
 Tamia Larkin   |    8 |      8
 Stewart Marks  |    5 |      5
 Ryann Parker   |    8 |      8
 Rudy Larkin    |    7 |      7
 Rodolfo Larkin |   10 |     10
 Novella Marks  |   10 |     10
 Markus Hirthe  |    2 |      2
 Mark Klein     |    2 |      2
```

```sql
yb_demo=# select m.name
          from (select to_char(generate_series, 'Month') AS name
                from generate_series(current_date-364, current_date, '1 month')) AS m
          where starts_with(m.name, 'J');
```

```output
   name
-----------
 January
 June
 July
```

## Something a bit more advanced

If you like a bit of a challenge, the following example URL escapes a string. There is some more room for tweaking in its current form, that is left for you to do.

```sql
yugabyte=# select string_agg(case
                              when to_hex(ascii(x.arr::text))::text
                                   in('20','23','24','25','26','40','60','2b','2c','2f','3a','3b','3c','3d','3e','3f',
                                   '5b','5c','5d','5e','7b','7c','7d') then '%' || to_hex(ascii(x.arr::text))::text
                              else x.arr
                              end, '') AS "url_escaped"
           from (select regexp_split_to_table('www.url.com/form?name="My name"&dob="1/1/2000"&email="hello@example.com"', '')) AS x (arr);
```

```output
                             url_escaped
------------------------------------------------------------------------------------------------
 www.url.com%2fform%3fname%3d"My%20name"%26dob%3d"1%2f1%2f2000"%26email%3d"hello%40example.com"
```
