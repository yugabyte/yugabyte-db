![PostgreSQL Anonymizer](images/png_RVB/PostgreSQL-Anonymizer_H_couleur.png)

Anonymization & Data Masking for PostgreSQL
===============================================================================

`postgresql_anonymizer` is an extension to mask or replace
[personally identifiable information] (PII) or commercially sensitive data from
a PostgreSQL database.

The project relies on a **declarative approach** of anonymization. This means
we're using the PostgreSQL Data Definition Language (DDL) in order to specify
the anonymization strategy inside the table definition itself.

Once the masking rules are defined, you can access the anonymized data in
different ways :

* [Anonymous Dumps] : Simply export the masked data into an SQL file
* [Static Masking] : Remove permanently the PII according to the rules
* [Dynamic Masking] : Hide PII only for the masked users
* [Generalization] : Reducing the accuracy of dates and numbers

In addition, various [Masking Functions] are available: randomization, faking,
partial scrambling, shuffling, noise, or even your own custom function!

Read the [Concepts] section for more details and [NEWS.md] for information
about the latest version.

[NEWS.md]: NEWS.md
[INSTALL]: https://postgresql-anonymizer.readthedocs.io/en/latest/INSTALL/
[Concepts]: https://postgresql-anonymizer.readthedocs.io/en/latest/concepts/
[personally identifiable information]: https://en.wikipedia.org/wiki/Personally_identifiable_information

[Anonymous Dumps]: https://postgresql-anonymizer.readthedocs.io/en/latest/anonymous_dumps/
[Static Masking]: https://postgresql-anonymizer.readthedocs.io/en/latest/static_masking/
[Dynamic Masking]: https://postgresql-anonymizer.readthedocs.io/en/latest/dynamic_masking/
[Masking Functions]: https://postgresql-anonymizer.readthedocs.io/en/latest/masking_functions/
[Generalization]: https://postgresql-anonymizer.readthedocs.io/en/latest/generalization/

Declaring The Masking Rules
------------------------------------------------------------------------------

The main idea of this extension is to offer **anonymization by design**.

The data masking rules should be written by the people who develop the
application because they have the best knowledge of how the data model works.
Therefore masking rules must be implemented directly inside the database schema.

This allows masking the data directly inside the PostgreSQL instance without
using an external tool and thus limiting the exposure and the risks of data leak.

The data masking rules are declared simply by using [security labels] :

```sql
=# CREATE EXTENSION IF NOT EXISTS anon CASCADE;

=# SELECT anon.init();

=# CREATE TABLE player( id SERIAL, name TEXT, points INT);

=# SECURITY LABEL FOR anon ON COLUMN player.name
-# IS 'MASKED WITH FUNCTION anon.fake_last_name()';

=# SECURITY LABEL FOR anon ON COLUMN player.id
-# IS 'MASKED WITH VALUE NULL';
```

[security labels]: https://www.postgresql.org/docs/current/sql-security-label.html

Static Masking
------------------------------------------------------------------------------

You can permanently remove the PII from a database with
`anon.anonymize_database()`. This will destroy the original data. Use with care.

```sql
=# SELECT * FROM customer;
 id  |   full_name      |   birth    |    employer   | zipcode | fk_shop
-----+------------------+------------+---------------+---------+---------
 911 | Chuck Norris     | 1940-03-10 | Texas Rangers | 75001   | 12
 112 | David Hasselhoff | 1952-07-17 | Baywatch      | 90001   | 423

=# SECURITY LABEL FOR anon ON COLUMN customer.full_name
-# IS 'MASKED WITH FUNCTION anon.fake_first_name() || '' '' || anon.fake_last_name()';

=# SECURITY LABEL FOR anon ON COLUMN customer.birth
-# IS 'MASKED WITH FUNCTION anon.random_date_between(''1920-01-01''::DATE,now())';

=# SECURITY LABEL FOR anon ON COLUMN customer.employer
-# IS 'MASKED WITH FUNCTION anon.fake_company()';

=# SECURITY LABEL FOR anon ON COLUMN customer.zipcode
-# IS 'MASKED WITH FUNCTION anon.random_zip()';

=# SELECT anon.anonymize_database();

=# SELECT * FROM customer;
 id  |     full_name     |   birth    |     employer     | zipcode | fk_shop
-----+-------------------+------------+------------------+---------+---------
 911 | michel Duffus     | 1970-03-24 | Body Expressions | 63824   | 12
 112 | andromach Tulip   | 1921-03-24 | Dot Darcy        | 38199   | 423

```

You can also use `anonymize_table()` and `anonymize_column()` to remove data
from a subset of the database.





Dynamic Masking
------------------------------------------------------------------------------

You can hide the PII from a role by declaring it as a "MASKED". Other roles
will still access the original data.

**Example**:

```sql
=# SELECT * FROM people;
 id | firstname | lastname |   phone
----+----------+----------+------------
 T1 | Sarah    | Conor    | 0609110911
(1 row)
```

Step 1 : Activate the dynamic masking engine

```sql
=# CREATE EXTENSION IF NOT EXISTS anon CASCADE;
=# SELECT anon.start_dynamic_masking();
```

Step 2 : Declare a masked user

```sql
=# CREATE ROLE skynet LOGIN;
=# SECURITY LABEL FOR anon ON ROLE skynet IS 'MASKED';
```

Step 3 : Declare the masking rules

```sql
=# SECURITY LABEL FOR anon ON COLUMN people.lastname
-# IS 'MASKED WITH FUNCTION anon.fake_last_name()';

=# SECURITY LABEL FOR anon ON COLUMN people.phone
-# IS 'MASKED WITH FUNCTION anon.partial(phone,2,$$******$$,2)';
```

Step 4 : Connect with the masked user

```sql
=# \! psql peopledb -U skynet -c 'SELECT * FROM people;'
 id | firstname | lastname  |   phone
----+----------+-----------+------------
 T1 | Sarah    | Stranahan | 06******11
(1 row)
```


Anonymous Dumps
------------------------------------------------------------------------------

Due to the core design of this extension, you cannot use `pg_dump` with a masked
user. If you want to export the entire database with the anonymized data, you
must use the `pg_dump_anon` command line. For example

```console
pg_dump_anon.sh -h localhost -p 5432 -U bob bob_db > dump.sql
```

For more details, read the [Anonymous Dumps] section.


Support
------------------------------------------------------------------------------

We need your feedback and ideas! Let us know what you think of this tool, how it
fits your needs and what features are missing.

You can either [open an issue] or send a message at <contact@dalibo.com>.

[open an issue]: https://gitlab.com/dalibo/postgresql_anonymizer/issues


Requirements
--------------------------------------------------------------------------------

This extension works with all [supported versions of PostgreSQL].

[supported versions of PostgreSQL]: https://www.postgresql.org/support/versioning/

It requires an extension called [pgcrypto] which is delivered by the
`postgresql-contrib` package of the main linux distributions.

[pgcrypto]: https://www.postgresql.org/docs/current/pgcrypto.html


Install
-------------------------------------------------------------------------------

See the [INSTALL] section


Limitations
------------------------------------------------------------------------------

* The dynamic masking system only works with one schema (by default `public`).
  When you start the masking engine with `start_dynamic_masking()`, you can
  specify the schema that will be masked with.
  **However** static masking with `anon.anonymize()`and [Anonymous Dumps] will
  work fine with multiple schemas.

* The [Anonymous Dumps] may not be consistent. Use [Static Masking] combined
  with `pg_dump` if you can't fence off your database from `DML` or `DDL`
  commands during the export.


Performance
------------------------------------------------------------------------------

See [docs/performances.md]

[docs/performances.md]: https://postgresql-anonymizer.readthedocs.io/en/latest/performances/




