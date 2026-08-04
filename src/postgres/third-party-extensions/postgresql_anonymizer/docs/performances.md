Performances
===============================================================================


Any anonymization process has a price as it will consume CPU time, RAM space and
probably a bunch of disk I/O... Here's a a quick overview of the question
depending on what strategy you are using....

In a nutshell, the anonymization performances will mainly depend on 2 important
factors:

* The size of the database
* The number of masking rules

Static Masking
------------------------------------------------------------------------------

Basically what static masking does it rewrite entirely the masked tables on
disk. This may be slow depending on your environment. And during this process,
the tables will be locked.

As an example: Anonymizing a 44GB database with 29 masking rules on an AWS EC2
instance takes approximately 25 minutes (see [MR 107] for more details).

[MR 107]: https://gitlab.com/dalibo/postgresql_anonymizer/-/issues/107#note_861963703

>
> In this case, the cost of anonymization is "paid" by all the users but it
> is paid **once and for all**.
>

Dynamic Masking
------------------------------------------------------------------------------

With dynamic masking, the real data is replaced on-the-fly **every time**  a
masked user sends a query to the database. This means that the masking users
will have slower response time than regular (unmasked) users. This is
generally ok because usually masked users are not considered as important as
the regular ones.

If you apply 3 or 4 rules to a table, the response time for the masked users
should approx. 20% to 30% slower than for the normal users.

As the masking rules are applied for each queries of the masked users, the
dynamic masking is appropriate when you have a limited number of masked users
that connect only from time to time to the database. For instance, a data
analyst connecting once a week to generate a business report.

If there are multiple masked users or if a masked user is very active, you
should probably export the masked data once-a-week on a secondary instance
and let these users connect to this secondary instance.

> In this case, the cost of anonymization is "paid" only by the masked users.



Anonymous Dumps
------------------------------------------------------------------------------

Some benchmarks made in march 2022 suggest that the `pg_dump_anon` wrapper is
twice as slow as the regular `pg_dump` tool.

If the backup process of your database takes 1 hour with `pg_dump`, then
anonymizing and exporting the entire database with `pg_dump_anon` will probably
take 2 hours.

> In this case, the cost of anonymization is "paid" by the user asking for the
> anonymous export. Other users of the database will not be affected.


How to speed things up ?
------------------------------------------------------------------------------

### Prefer `MASKED WITH VALUE` whenever possible

It is always faster to replace the original data with a static value instead
of calling a masking function.

### Sampling

If you need to anonymize data for testing purpose, chances are that a smaller
subset of your database will be enough. In that case, you can easily speed up
the anonymization by downsizing the volume of data.

Checkout the [Sampling] section for more details.



### Materialized Views

Dynamic masking is not always required! In some cases, it is more efficient
to build [Materialized Views] instead.

For instance:

```sql
CREATE MATERIALIZED VIEW masked_customer AS
SELECT
    id,
    anon.random_last_name() AS name,
    anon.random_date_between('1920-01-01'::DATE,now()) AS birth,
    fk_last_order,
    store_id
FROM customer;
```

[Materialized Views]: https://www.postgresql.org/docs/current/static/sql-creatematerializedview.html
