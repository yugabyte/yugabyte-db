Privacy By Default
===============================================================================

Disclaimer
--------------------------------------------------------------------------------

**This feature is considered in beta and not ready for production until version
2.O is published.**

**Use with care.**

Principle
--------------------------------------------------------------------------------

The GDPR regulation (and other privacy laws) introduces the concept of
[data protection by default]. In a nutshell, it means that **by default**,
organisations should ensure that data is processed with the highest privacy
protection so that by default personal data isn’t made accessible to an
indefinite number of persons.

By applying this principle to anonymization, we end up with the idea of **privacy
by default** which basically means that all columns of all tables should be
masked by default, without having to declare a masking rule for each of them.

To enable this feature, simply set the option `anon.privacy_by_default` to `on`.

[data protection by default]: https://ec.europa.eu/info/law/law-topic/data-protection/reform/rules-business-and-organisations/obligations/what-does-data-protection-design-and-default-mean_en

Example
--------------------------------------------------------------------------------

Imagine a database named `foo` with a basic table containing HTTP logs:

```sql
# SELECT * FROM access_logs LIMIT 1;
      date_open      |     ip_addr     |    url     |        browser_agent
---------------------+-----------------+------------+------------------------------
 2009-01-08 00:00:00 | 192.168.100.128 | /home.html | Mozilla/5.0 (Windows; en_US)
(1 row)
```

Now let's activate privacy by default:

```sql
ALTER DATABASE foo SET anon.privacy_by_default = True;
```

The setting will be applied for the next sessions and we can now anonymize the
table without writing any masking rule.

```sql
# SELECT anon.anonymize_database();
 anonymize_database
--------------------
 t

# SELECT * FROM access_logs LIMIT 1;
 date_open | ip_addr | url | browser_agent
-----------+---------+-----+---------------
           |         |     | unkown
```


Unmasking columns
--------------------------------------------------------------------------------

As we can see, when the `anon.privacy_by_default` is defined all the values will
be replaced by the column's default value or NULL. The entire dataset is
destroyed.

Now instead of writing rules to mask the sensible columns, we will write rules
to **unmask** the ones we want to allow.

For instance, let's say that we want to keep the authentic value of the `url`
field, we can simply "unmask" the column like this:

```sql
SECURITY LABEL FOR anon ON COLUMN access_logs.url
IS 'NOT MASKED';
```

This can also be achieved by a masking rule that will replace the value with
itself:

```sql
SECURITY LABEL FOR anon ON COLUMN access_logs.url
IS 'MASKED WITH VALUE url';
```

Now we'd like to unmask the `date_open` field in the anonymized dataset but
we need to generalize the dates to keep only the year:

```sql
SECURITY LABEL FOR anon ON COLUMN access_logs.date_open
IS 'MASKED WITH FUNCTION make_date(EXTRACT(year FROM date_open)::INT,1,1)';
```



Caveat: Add a DEFAULT to the NOT NULL columns
--------------------------------------------------------------------------------

It is a bit ironic that the `anon.privacy_by_default` parameter **is not**
enabled by default. This reason is simple: activating this option **may or may
not** lead to contraint violations depending on the columns constraints placed
in the database model.

Let's say we want to add a `NOT NULL` constraint on the `date_open` column:

```sql
ALTER TABLE public.access_logs
  ALTER COLUMN date_open
  SET NOT NULL;
```

Now if we try to anonymize the table, we get the following violation:

```sql
SELECT anon.anonymize_table('public.access_logs') as test4;
ERROR:  Cannot mask a "NOT NULL" column with a NULL value
HINT:  If privacy_by_design is enabled, add a default value to the column
```

The solution here is simply to define a default value and this value will be
used for the `privacy_by_default` mechanism.

```sql
ALTER TABLE public.access_logs
  ALTER COLUMN date_open
  SET DEFAULT now();
```

Other constraints (foreign keys, UNIQUE, CHECK, etc.) should work fine without
a DEFAULT value.


