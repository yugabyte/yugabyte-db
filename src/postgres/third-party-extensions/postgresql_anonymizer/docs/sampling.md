Sampling
===============================================================================

Principle
-------------------------------------------------------------------------------

The GDPR introduces the concept of "[data minimization]" which means that the
collection of personal information must be limited to what is directly relevant
and necessary to accomplish a specified purpose.

[data minimization]: https://edps.europa.eu/data-protection/data-protection/glossary/d_en

If you're writing an anonymization policy for a dataset, chances are that you
don't need to anonymize **the entire database**. In most cases, extract a
subset of the table is sufficient. For example, if you want to export an
anonymous dumps of the data for testing purpose in a CI workflow, extracting
and masking only 10% of the database may be enough.

Furthermore, anonymizing a smaller portion (i.e a "sample") of the dataset will
be way faster.


With PostgreSQL Anonymizer, you can use 2 different sampling methods :

* [Sampling with TABLESAMPLE](#sampling-with-tablesample)
* [Sampling with RLS Policies](#sampling-with-rls-policies)

You can also [Truncate Tables for the masked users] !

[Truncate Tables for the masked users]: #truncate-tables-for-the-masked-users


Sampling with TABLESAMPLE
-------------------------------------------------------------------------------

Let's say you have a huge amounts of http logs stored in a table. You want to
remove the ip addresses and extract only 10% of the table:

```sql
CREATE TABLE http_logs (
  id integer NOT NULL,
  date_opened DATE,
  ip_address INET,
  url TEXT
);

SECURITY LABEL FOR anon ON COLUMN http_logs.ip_address
IS 'MASKED WITH VALUE NULL';

SECURITY LABEL FOR anon ON TABLE http_logs
IS 'TABLESAMPLE BERNOULLI(10)';
```

Now you can either do static masking, dynamic masking or an anonymous dumps.
The mask data will represent a 10% portion of the real data.


The syntax is exactly the same as the [TABLESAMPLE clause] which can be placed
at the end of a [SELECT] statement.

[TABLESAMPLE clause]: https://wiki.postgresql.org/wiki/TABLESAMPLE_Implementation
[SELECT]: https://www.postgresql.org/docs/current/sql-select.html

You can also defined a sampling ratio at the database-level and it will be
applied to all the tables that don't have their own `TABLESAMPLE` rule.

```sql
SECURITY LABEL FOR anon ON DATABASE app
IS 'TABLESAMPLE SYSTEM(33)';
```


Sampling with RLS policies
-------------------------------------------------------------------------------

Another approach for sampling is to use [Row Level Security Policies], also
known as `RLS` or `Row Security Policies`.

[Row Level Security Policies]: https://www.postgresql.org/docs/current/ddl-rowsecurity.html

Let's use the same example as a above, this time we want to define a limit so
the mask users can only see the logs of the last 6 months.


```sql
CREATE TABLE http_logs (
  id integer NOT NULL,
  date_opened DATE,
  ip_address INET,
  url TEXT
);

SECURITY LABEL FOR anon ON COLUMN http_logs.ip_address
  IS 'MASKED WITH VALUE NULL';

ALTER TABLE http_logs ENABLE ROW LEVEL SECURITY;

CREATE POLICY http_logs_sampling_for_masked_users
  ON http_logs
  USING (
    NOT anon.hasmask(CURRENT_USER::REGROLE)
    OR date_opened >= now() - '6 months'::INTERVAL
  );

```

This RLS policy is based on 2 conditions:

* if the current user is not masked, the first condition is true and
  he/she can read all the lines

* if the current user is masked, the first condition is false
  and he/she can only read the lines that satisfy the second condition


Sampling with RLS policies is more powerful than the TABLESAMPLE method,
however maintaining a set of RLS policies is known to be difficult in the long
run. The benefits from Postgres RLS can dissipate when the size of the
organization, the amount of data collected, and the number of restrictions
grow in size and complexity.


Maintaining Referential Integrity
-------------------------------------------------------------------------------

!!! note

    The sampling methods described above **MAY FAIL** if you have
    foreign keys pointing at the table you want to sample.

Extracting a subset of a database while maintaining referential integrity is
tricky and it is not supported by this extension.

If you really need to keep referential integrity in an anonymized dataset, you
need to do it in 2 steps:

* First, extract a sample with [pg_sample]
* Second, anonymize that sample

There may be other sampling tools for PostgreSQL but [pg_sample] is probably
the best one.

[pg_sample]: https://github.com/mla/pg_sample


Truncate Tables for the masked users
-------------------------------------------------------------------------------

In certain situations, you can also erase completely a table instead of just
masking some of the columns.

For instance, let's say that masked users should not see anything in the
`http_logs` table below

```sql
CREATE TABLE http_logs (
  id integer NOT NULL,
  date_opened DATE,
  ip_address INET,
  url TEXT
);
```

Using the [TABLESAMPLE clause], you can simply set the sampling ratio to 0


```sql
SECURITY LABEL FOR anon ON TABLE http_logs IS ' TABLESAMPLE SYSTEM (0)';
```

Now the table will be erased for the masked users !

```sql
SET ROLE the_database_owner;

SELECT count(*) FROM http_logs;
  count
---------
 156706

SET ROLE a_masked_user;

SELECT count(*) FROM http_logs;
  count
---------
     0
```
