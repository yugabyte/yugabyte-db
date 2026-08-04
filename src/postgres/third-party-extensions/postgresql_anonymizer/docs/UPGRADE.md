Upgrade
==============================================================================


Currently there's no way to upgrade easily from a version to another.
The operation `ALTER EXTENSION ... UPDATE ...` is not supported.

You need to drop and recreate the extension after every upgrade.


Upgrade to version 1.3 and further versions
-------------------------------------------------------------------------------

Starting with version 1.3, the extension enforces a series of security checks
and it will refuse some masking rules that were previously accepted.

Here's a few example of the changes you may need to make to your masking policy



### Using custom masking functions

If you have developed custom masking functions, you now need to place them
inside a dedicated schema and declare that this schema is `trusted`

For example, let's say you have a function `remove_phone` that delete phone
numbers from a `JSONB` field

First create a schema:

```sql
CREATE SCHEMA IF NOT EXISTS my_masks;
```

Then a superuser must declare it as trusted:

```sql
SECURITY LABEL FOR anon ON SCHEMA my_masks IS 'TRUSTED';
```

Now you can write the function:

```sql
CREATE OR REPLACE FUNCTION my_masks.remove_phone(j JSONB)
RETURNS JSONB
AS $$
  SELECT j - ARRAY['phone']
$$
LANGUAGE SQL ;
```

And finally use it in a masking rule:

```sql
SECURITY LABEL FOR anon ON COLUMN player.personal_details
  IS 'MASKED WITH FUNCTION my_masks.remove_phone(personal_details)';
```

See the [Write your own Masks !] section of the doc for more details...

[Write your own Masks !]: masking_functions.md#write-your-own-masks


### Using `pg_catalog` functions


With version 1.3 and later, the `pg_catalog` schema is not longer trusted because
it contains [system administration functions] that should not be used
as masking functions.

[system administration functions]: https://www.postgresql.org/docs/current/functions-admin.html

However the extension provides bindings to some useful and safe commodity
functions from the `pg_catalog` schema.

For instance, the following rule

```sql
SECURITY LABEL FOR anon ON COLUMN employee.phone
  IS 'MASKED WITH FUNCTION md5(phone)'
```

```sql
SECURITY LABEL FOR anon ON COLUMN employee.phone
  IS 'MASKED WITH FUNCTION anon.md5(phone)'
```

See the [Using `pg_catalog` functions] section of the doc for more details...

[Using `pg_catalog` functions]: masking_functions.md#Using-pg_catalog-functions

### Operators

The `MASKED WITH FUNCTION` syntax is now more strict and in particular operators
are not allowed as a masking value.

For instance, until version 1.3

```sql
SECURITY LABEL FOR anon ON COLUMN player.name
  IS 'MASKED WITH FUNCTION anon.fake_first_name() || anon.fake_last_name()';
```

Now operators must be replaced by an actual function. For instance, the `||`
operator would be replaced by `anon.concat`

```sql
SECURITY LABEL FOR anon ON COLUMN player.name
  IS 'MASKED WITH FUNCTION anon.concat(anon.fake_first_name(),anon.fake_last_name())';
```

### Conditional masking rules

The `MASKED WITH VALUE CASE WHEN ...` was never an intended feature but it
work by accident.

Until version 1.3, the syntax below was accepted:

```sql
SECURITY LABEL FOR anon ON COLUMN player.score
  IS 'MASKED WITH VALUE CASE WHEN score IS NULL
                             THEN NULL
                             ELSE anon.random_int_between(0,100)
                             END';
```

The `CASE` syntax is now rejected and can be replaced by the `anon.ternary()`
function:

```sql
SECURITY LABEL FOR anon ON COLUMN player.score
  IS 'MASKED WITH FUNCTION anon.ternary(score IS NULL,
                                        NULL,
                                        anon.random_int_between(0,100)
  )';
```

See the [Conditional Masking] section of the doc for more details...

[Conditional Masking]: masking_functions.md#conditional-masking
