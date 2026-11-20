Put on your Masks !
===============================================================================

The main idea of this extension is to implement the concept of
**[Privacy by Design]**, which is principle imposed by the
[Article 25 of the GDPR].

[Privacy by Design]: https://en.wikipedia.org/wiki/Privacy_by_design
[Article 25 of the GDPR]: https://gdpr-info.eu/art-25-gdpr/

With PostgreSQL Anonymizer, you can declare a **masking policy** which is a set
of **masking rules** stored inside the database model and applied to various
database objects.

The data masking rules should be written by the people who develop the
application because they have the best knowledge of how the data model works.
Therefore masking rules must be implemented directly inside the database schema.

This allows to mask the data directly inside the PostgreSQL instance without
using an external tool and thus limiting the exposure and the risks of data leak.

The data masking rules are declared simply by using [security labels]:

[security labels]: https://www.postgresql.org/docs/current/sql-security-label.html

<!-- demo/declare_masking_rules.sql -->

```sql
CREATE TABLE player( id SERIAL, name TEXT, total_points INT, highest_score INT);

INSERT INTO player VALUES
  ( 1, 'Kareem Abdul-Jabbar', 38387, 55),
  ( 5, 'Michael Jordan', 32292, 69);

SECURITY LABEL FOR anon ON COLUMN player.name
  IS 'MASKED WITH FUNCTION anon.fake_last_name()';

SECURITY LABEL FOR anon ON COLUMN player.id
  IS 'MASKED WITH VALUE NULL';
```

Principles
------------------------------------------------------------------------------

* You can mask tables in multiple schemas

* Generated columns are respected.

* [Row Security Policies] aka `RLS` are respected.

* A masking rule may break data integrity. For instance, you can mask a
  `NOT NULL` column with the value `NULL`. This is up to you to decide
  whether or not the masked users need data integrity.

* You need to declare masking rules on views. By default, the masking rules
  declared on the underlying tables are **NOT APPLIED** on the view. For
  instance, if a view `v_foo` is based upon a table `foo`, then the masking
  rules of table `foo` will not be applied to `v_foo`. You will need to declare
  specific masking rules for `v_foo`. Remember that PostgreSQL uses the view
  owner (not the current user) to check permissions on the underlying tables.

[Row Security Policies]: https://www.postgresql.org/docs/current/ddl-rowsecurity.html

Escaping String literals
------------------------------------------------------------------------------

As you may have noticed the masking rule definitions are placed between single
quotes. Therefore if you need to use a string inside a masking rule, you need
to use [C-Style escapes] like this:

```sql
SECURITY LABEL FOR anon ON COLUMN player.name
  IS E'MASKED WITH VALUE \'CONFIDENTIAL\'';
```

Or use [dollar quoting] which is easier to read:

```sql
SECURITY LABEL FOR anon ON COLUMN player.name
  IS 'MASKED WITH VALUE $$CONFIDENTIAL$$';
```

[C-Style escapes]: https://www.postgresql.org/docs/current/sql-syntax-lexical.html#SQL-SYNTAX-STRINGS-ESCAPE
[dollar quoting]: https://www.postgresql.org/docs/current/sql-syntax-lexical.html#SQL-SYNTAX-DOLLAR-QUOTING


Listing masking rules
------------------------------------------------------------------------------

To display all the masking rules declared in the current database, check out
the `anon.{all|sys|user}_rules` views:

```sql
SELECT * FROM anon.all_rules;
SELECT * FROM anon.sys_rules;
SELECT * FROM anon.user_rules;
```

Debugging masking rules
------------------------------------------------------------------------------

When an error occurs to due a wrong masking rule, you can get more detailed
information about the problem by setting `client_min_messages` to `DEBUG` and
you will get useful details

``` sql
postgres=# SET client_min_messages=DEBUG;
SET
postgres=# SELECT anon.anonymize_database();
DEBUG:  Anonymize table public.bar with firstname = anon.fake_first_name()
DEBUG:  Anonymize table public.foo with id = NULL
ERROR:  Cannot mask a "NOT NULL" column with a NULL value
HINT:  If privacy_by_design is enabled, add a default value to the column
CONTEXT:  PL/pgSQL function anon.anonymize_table(regclass) line 47 at RAISE
SQL function "anonymize_database" statement 1
```

Removing a masking rule
------------------------------------------------------------------------------

You can simply erase a masking rule like this:

```sql
SECURITY LABEL FOR anon ON COLUMN player.name IS NULL;
```

To remove all rules at once, you can use:

```sql
SELECT anon.remove_masks_for_all_columns();
```

Custom Values for a Masking Policy
------------------------------------------------------------------------------

In some case, you may have to apply variations to masking policy based on a
set of parameters. In other words, custom values in PostgreSQL Anonymizer
allow you to parameterize masking rules so that the same masking policy can
behave differently based on context - such as the country, environment, or
customer profile.

Define the custom values as a JSON dictionary:

```sql
SET anon.custom_values
TO '{ "country": "France", "locale": "fr_FR" }';
```

You can then use the `anon.custom_value()` function in your masking rule
Alternavely there's also a `anon.custom_value(key,default)` variant.

```sql
SECURITY LABEL FOR anon ON COLUMN player.name
IS 'MASKED WITH FUNCTION anon.fake_last_name_locale(anon.custom_value('locale'))';
```

Custom values defined this way are returned as `text`, depending on
the context, you might have to use the appropriate cast to remain
compatible with some functions and assignments or keep your predicate
indexable.

Finally you can define the custom_values on a per-database or on a per-user
basis.

```sql

ALTER DATABASE french_stores
SET anon.custom_values
TO '{ "country": "France", "locale": "fr_FR" }';

ALTER DATABASE italian_stores
SET anon.custom_values
TO '{ "country": "Italia", "locale": "it_IT" }';

```



Multiple Masking Policies
------------------------------------------------------------------------------

By default, there is only one masking policy named 'anon'. Most of the times,
a single policy is enough. However in more complex situations, the database
owner may want to define different sets of masking rules for different use
cases.

This can be achieved by declaring multiple masking policies.

For instance, we can add 2 new policies with:

```sql
ALTER DATABASE foo SET anon.masking_policies TO 'devtests, analytics';
```

> Important:
> You need to reconnect to the database so that the change takes effect !

We can now define a "devtests" policy for a developer name "devin". Devin wants
to run CI tests on his code using fake/random data.


```sql
SECURITY LABEL FOR devtests ON COLUMN player.name
  IS 'MASKED WITH FUNCTION anon.fake_last_name()';

SECURITY LABEL FOR devtests ON COLUMN player.highest_score
  IS 'MASKED WITH FUNCTION anon.random_int_between(0,50)';

SECURITY LABEL FOR devtests ON ROLE devin IS 'MASKED';
```

We can also define an "analytics" for a data scientist name "Anna". Anna needs
to run global stats over the dataset, she want to keep the real value on the
`highest_score` column but she does not need to know the players names


```sql
SECURITY LABEL FOR analytics ON COLUMN player.name
  IS 'MASKED WITH VALUE NULL';

SECURITY LABEL FOR analytics ON ROLE anna IS 'MASKED';
```

Only one policy can be applied to a role. If you define that a role is masked
in several masking policies, only the first one in the list will be applied.

The "anon" policy is always declared and cannot be removed.

If you declare a function as `TRUSTED`, it will be trusted for all masking
policies.


Limitations
------------------------------------------------------------------------------

* The masking rules are **NOT INHERITED** ! If you have split a table into
  multiple partitions, you need to declare the masking rules for each partition.

* Masking identity columns is tricky. If an identity column is defined as
  `GENERATED ALWAYS`, then static masking will not work on that column. Note
  identity columns are used most of the time for surrogate keys ( also known
  as "factless keys" ) and in general those keys should not required to be
  masked. However if you really need to mask and identity column you can
  redefine it as `GENERATED DEFAULT`.
