Hide sensitive data from a "masked" user
===============================================================================

You can hide some data from a role by declaring this role as a "MASKED" one.
Other roles will still access the original data.

**Example**:

<!-- demo/masking.sql -->

```sql
CREATE TABLE people ( id TEXT, firstname TEXT, lastname TEXT, phone TEXT);
INSERT INTO people VALUES ('T1','Sarah', 'Conor','0609110911');
SELECT * FROM people;

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
=# SECURITY LABEL FOR anon ON ROLE skynet
-# IS 'MASKED';
```

Step 3 : Declare the masking rules

```sql
SECURITY LABEL FOR anon ON COLUMN people.name
IS 'MASKED WITH FUNCTION anon.random_last_name()';

SECURITY LABEL FOR anon ON COLUMN people.phone
IS 'MASKED WITH FUNCTION anon.partial(phone,2,$$******$$,2)';
```


Step 4 : Connect with the masked user

```sql
=# \c - skynet
=> SELECT * FROM people;
 id | firstname | lastname  |   phone
----+----------+-----------+------------
 T1 | Sarah    | Stranahan | 06******11
(1 row)
```

How to change the type of a masked column
------------------------------------------------------------------------------

When dynamic masking is activated, you are not allowed to change the datatype
of a column if there's a mask upon it.

To modify a masked column, you need to switch of temporarily the masking engine
like this:

```sql
BEGIN;
SELECT anon.stop_dynamic_masking();
ALTER TABLE people ALTER COLUMN phone TYPE VARCHAR(255);
SELECT anon.start_dynamic_masking();
COMMIT;
```


How to drop a masked table
------------------------------------------------------------------------------

The dynamic masking engine will build _masking views_ upon the masked tables.
This means that it is not possible to drop a masked table directly. You will
get an error like this :

```sql
# DROP TABLE people;
psql: ERROR:  cannot drop table people because other objects depend on it
DETAIL:  view mask.company depends on table people
```

To effectively remove the table, it is necessary to add the `CASCADE` option,
so that the masking view will be dropped too:

```sql
DROP TABLE people CASCADE;
```

How to unmask a role
------------------------------------------------------------------------------

Simply remove the security label like this:

```sql
SECURITY LABEL FOR anon ON ROLE bob IS NULL;
```

To unmask all masked roles at once you can type:

```sql
SELECT anon.remove_masks_for_all_roles();
```



Limitations
------------------------------------------------------------------------------

### Listing the tables

Due to how the dynamic masking engine works, when a masked role will try to
display the tables in psql with the `\dt` command, then psql will not show any
tables.

This is because the `search_path` of the masked role is rigged.

You can try adding explicit schema you want to search, for instance:

```sql
\dt *.*
\dt public.*
```

### Only one schema

The dynamic masking system only works with one schema (by default `public`).
When you start the masking engine with `start_dynamic_masking()`, you can
specify the schema that will be masked with:

```sql
ALTER DATABASE foo SET anon.sourceschema TO 'sales';
```

Then open a new session to the database and type:

```sql
SELECT start_dynamic_masking();
```

**However** static masking with `anon.anonymize()`and anonymous export
with `anon.dump()` will work fine with multiple schemas.

### Performances

Dynamic Masking is known to be very slow with some queries, especially if you
try to join 2 tables on a masked key using hashing or pseudonymization.

### Graphic Tools

When you are using a masked role with a graphic interface such as DBeaver or
pgAdmin, the "data" panel may produce the following error when trying to display
the content of a masked table called `foo`:

```bash
SQL Error [42501]: ERROR: permission denied for table foo
```

This is because most of these tools will directly query the `public.foo` table
instead of being "redirected" by the masking engine toward the `mask.foo` view.

In order the view the masked data with a graphic tool, you can either:

1- Open the SQL query panel and type `SELECT * FROM foo`

2- Navigate to `Database > Schemas > mask > Views > foo`

