Sampling
===============================================================================

Principle
-------------------------------------------------------------------------------

The GDPR introduces the concept of principle of "[data minimisation]" which
means that the collection of personal information must be limited to what
is directly relevant and necessary to accomplish a specified purpose.

[data minimization]: https://edps.europa.eu/data-protection/data-protection/glossary/d_en

If you're writing an anonymization policy for a dataset, chances are that you
don't need to anonymize **the entire database**. In most cases, extract a
subset of the table is sufficient. For example, if you want to export an
anonymous dumps of the data for testing purpose in a CI workflow, extracting
and masking only 10% of the database may be enough.

Furthermore, anonymizing a smaller portion (i.e a "sample") of the dataset will
be way faster.


Example
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


Syntax
-------------------------------------------------------------------------------

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

Maintaining Referential Integrity
-------------------------------------------------------------------------------

> **NOTE** :The sampling method describe above **WILL FAIL** if you have
> foreign keys pointing at the table you want to sample.

Extracting a subset of a database while maintaining referential integrity is
tricky and it is not supported by this extension.

If you really need to keep referential integrity in an anonymized dataset, you
need to do it in 2 steps:

* First, extract a sample with [pg_sample]
* Second, anonymize that sample

There may be other sampling tools for PostgreSQL but [pg_sample] is probably
the best one.

[pg_sample]: https://github.com/mla/pg_sample

