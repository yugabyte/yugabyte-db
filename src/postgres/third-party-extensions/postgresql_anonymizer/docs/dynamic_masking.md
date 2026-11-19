Hide sensitive data from a "masked" user
===============================================================================

You can hide some data from a role by declaring this role as "MASKED".

Other roles will still access the original data.

![PostgreSQL Dynamic Masking](images/anon-Dynamic.drawio.png)

**Example**:

<!-- demo/masking.sql -->

```sql
CREATE TABLE people ( id TEXT, firstname TEXT, lastname TEXT, phone TEXT);
INSERT INTO people VALUES ('T1','Sarah', 'Conor','0609110911');
SELECT * FROM people;

=# SELECT * FROM people;
 id | firstname | lastname |   phone
----+-----------+----------+------------
 T1 | Sarah     | Conor    | 0609110911
(1 row)
```

Step 1 : Activate the dynamic masking engine

```sql
=# CREATE EXTENSION IF NOT EXISTS anon CASCADE;
=# ALTER DATABASE foo SET anon.transparent_dynamic_masking TO true;
```

Step 2 : Declare the masking rules

```sql
SECURITY LABEL FOR anon ON COLUMN people.name
IS 'MASKED WITH FUNCTION anon.dummy_last_name()';

SECURITY LABEL FOR anon ON COLUMN people.phone
IS 'MASKED WITH FUNCTION anon.partial(phone,2,$$******$$,2)';
```

Step 3 : Declare a masked user with read access

```sql
=# CREATE ROLE skynet LOGIN;
=# SECURITY LABEL FOR anon ON ROLE skynet IS 'MASKED';
```

```sql
GRANT pg_read_all_data to skynet;
```

**NOTE:** If you want a more fine-grained access policy you can grant access
more precisely, for instance:

```sql
GRANT USAGE ON SCHEMA public TO skynet;
GRANT SELECT ON ALL TABLES IN SCHEMA public TO skynet;
-- etc.
```




Step 4 : Connect with the masked user

```sql
=# \c - skynet
=> SELECT * FROM people;
 id | firstname | lastname  |   phone
----+-----------+-----------+------------
 T1 | Sarah     | Stranahan | 06******11
(1 row)
```

Principles
------------------------------------------------------------------------------

* Masked roles should not be allowed to insert, update or delete data.
* You can mask table in multiple schemas.
* Generated columns are respected.
* You can apply [Row Security Policies] aka `RLS` to a masked role.
* A masking rule may break data integrity. For instance, you can mask a column
  having a UNIQUE constraint with the value `NULL`. This is up to you to decide
  whether or not the mask users need data integrity.

[Row Security Policies]: https://www.postgresql.org/docs/current/ddl-rowsecurity.html


Limitations
------------------------------------------------------------------------------

* Masked roles are not allowed to use EXPLAIN


How to unmask a role
------------------------------------------------------------------------------

Simply remove the security label like this:

```sql
SECURITY LABEL FOR anon ON ROLE bob IS NULL;
```
