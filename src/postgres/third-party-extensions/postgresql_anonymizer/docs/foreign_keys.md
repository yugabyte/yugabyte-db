Masking Foreign Keys
===============================================================================

TLDR; Masking a foreign key is hard. There is no silver buller but this chapter
describes multiple strategies you can apply depending on your context.

First of all : Avoid Natural Keys at all costs
-------------------------------------------------------------------------------

This is the best advice you will find here.

**A primary or a foreign key should not have any meaning outside of the database
itself. Therefore you should not have to anonymize it.**

If you really need to anonymize a primary/foreign key in a table, this means
that it is a natural key (as opposed to a [surrogate key]).

[surrogate key]: https://en.wikipedia.org/wiki/Surrogate_key

Natural keys are problematic for many reasons:

* they can change over time (like email addresses or product codes), forcing
  cascading updates throughout related tables
* they're often not truly unique in practice, even seemingly unique values
  like SSNs can have duplicates or exceptions
* they tend to be longer and more complex than simple integers
* they make joins slower and indexes larger
* they can contain sensitive information that you might not want exposed in
  URLs or logs.
* they may change whenever business rules evolve, requiring database
  restructuring.

Surrogate keys (i.e. auto-incrementing integers) avoid these issues by
providing stable, meaningless identifiers that never need to change.

If you have implemented surrogate keys, you don't need to read this chapter
any further ;-)


Example
-------------------------------------------------------------------------------

<!-- tests/sql/foreign_keys.sql -->

Let's consider this (dumb) example

``` sql
CREATE TABLE author (
  id SERIAL UNIQUE,
  name TEXT
);

CREATE TABLE book (
  id SERIAL UNIQUE,
  name TEXT,
  fk_author_id INTEGER,
  FOREIGN KEY (fk_author_id) REFERENCES author(id)
);
```

ON UPDATE CASCADE
-------------------------------------------------------------------------------

For [static masking], the best solution is add the `ON UPDATE CASCADE` action
to the foreign key :

```sql
ALTER TABLE book DROP CONSTRAINT book_fk_author_name_fkey;

ALTER TABLE book
ADD CONSTRAINT book_fk_author_name_fkey
    FOREIGN KEY (fk_author_name)
    REFERENCES author(name)
    ON UPDATE CASCADE
;
```

And then define a masking rule only for the referenced column

```sql
SECURITY LABEL FOR anon ON COLUMN author.name
  IS 'MASKED WITH FUNCTION anon.dummy_name()';
```

Now whenever the `author` table will be anonymized, the `book.fk_author_name`
column will be altered automatically. Therefore you should not declare
a masking rule on `book.fk_author_name` !

[static masking]: static_masking.md



Pseudonymization
-------------------------------------------------------------------------------

However the `ON UPDATE CASCADE` action is only triggered when the real data is
actually altered, so it will not work for [dynamic masking], [backup masking]
and [replica masking].

[dynamic masking]: dynamic_masking.md
[backup masking]: anonymous_dumps.md
[replica masking]: replica_masking.md

In these situations, the best approach is to use a [pseudonymization] function

[pseudonymization]: masking_functions.md#pseudonymization

First of all declared that the foerign key is deferrable:

```sql
ALTER TABLE book DROP CONSTRAINT book_fk_author_name_fkey;

ALTER TABLE book
ADD CONSTRAINT book_fk_author_name_fkey
    FOREIGN KEY (fk_author_name)
    REFERENCES author(name)
    DEFERRABLE
;
```

Then declare a similar masking rule for each column:

```sql

SECURITY LABEL FOR anon ON COLUMN author.name
  IS 'MASKED WITH FUNCTION pg_catalog.md5(name)';

SECURITY LABEL FOR anon ON COLUMN book.fk_author_name
  IS 'MASKED WITH FUNCTION pg_catalog.md5(fk_author_name)';

```

However keep in mind that that [Pseudonymization Is Not Anonymization] !

[Pseudonymization Is Not Anonymization]: masking_functions.md#pseudonymization
