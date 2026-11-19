Backup Masking ( aka Anonymous Dumps )
===============================================================================


![PostgreSQL Anonymous Dumps](images/anon-Dump.drawio.png)

Transparent Anonymous Dumps
------------------------------------------------------------------------------

To export the anonymized data from a database, follow these 3 steps:

### 1. Create a masked user

```sql
CREATE ROLE anon_dumper LOGIN PASSWORD 'x';
ALTER ROLE anon_dumper SET anon.transparent_dynamic_masking = True;
SECURITY LABEL FOR anon ON ROLE anon_dumper IS 'MASKED';
```

__NOTE:__ You can replace the name `anon_dumper` by another name.


### 2. Grant read access to that masked user

```sql
GRANT pg_read_all_data to anon_dumper;
```

__NOTE:__ If you want a more fine-grained access policy you can grant access
more precisely, for instance:


```sql
GRANT USAGE ON SCHEMA public TO anon_dumper;
GRANT SELECT ON ALL TABLES IN SCHEMA public TO anon_dumper;
GRANT SELECT ON ALL SEQUENCES IN SCHEMA public TO anon_dumper;

GRANT USAGE ON SCHEMA foo TO anon_dumper;
GRANT SELECT ON ALL TABLES IN SCHEMA foo TO anon_dumper;
GRANT SELECT ON ALL SEQUENCES IN SCHEMA foo TO anon_dumper;
```


### 3. Launch pg_dump with the masked user

Now to export the anonymous data from a database named `foo`, let's use
`pg_dump`:

```bash
pg_dump foo \
        --user anon_dumper \
        --no-security-labels \
        --exclude-extension="anon" \
        --file=foo_anonymized.sql
```

__NOTES:__

* linebreaks are here for readability

* `--no-security-labels` will remove the masking rules from the anonymous dump.
  This is really important because masked users should not have access to the
  masking policy.

* `--exclude-extension` is only available with `pg_dump 17` and later.
  As an alternative you can use `--extension plpgsql`.

* `--format=custom` is supported


Anonymizing an SQL file
-----------------------------------------------------------------------------

![Anon Files image](images/anon-Files.drawio.png)

> In previous versions of the documentation, this method was also called
> « anonymizing black box ».

You can also apply masking rules directly on a database backup file !

The PostgreSQL Anonymizer docker image contains a specific entrypoint script
called `/dump.sh`. You pass the original data and the masking rules to
to that `/dump.sh` script and it will return an anonymized dump.

Here's an example in 4 steps:

_Step 1:_ Dump your original data (for instance `dump.sql`)

```console
pg_dump --format=plain [...] my_db > dump.sql
```

Note this method only works with plain sql format (`-Fp`). You **cannot**
use the custom format (`-Fc`) and the directory format (`-Fd`) here.

If you want to maintain the owners and grants, you need export them with
`pg_dumpall --roles-only` like this:

```console
(pg_dumpall -Fp [...] --roles-only && pg_dump -Fp [...] my_db ) > dump.sql
```

_Step 2:_ Write your masking rules in a separate file (for instance `rules.sql`)

```sql
RESET search_path;

SECURITY LABEL FOR anon ON COLUMN people.lastname
  IS 'MASKED WITH FUNCTION anon.dummy_last_name()';

-- etc.
```

_Step 3:_ Pass the dump and the rules through the docker image and receive an
anonymized dump !

```console
IMG=registry.gitlab.com/dalibo/postgresql_anonymizer:stable
ANON="docker run --rm --interactive $IMG /dump.sh"
cat dump.sql rules.sql | $ANON > anon_dump.sql
```

(this last step is written on 3 lines for clarity)

_NB:_ You can also gather _step 1_ and _step 3_ in a single command:

```console
(pg_dumpall --roles-only && pg_dump my_db && cat rules.sql) | $ANON > anon_dump.sql
```

__NOTES:__

You can use most the [pg_dump output options] with the `/dump.sh` script,
for instance:

```console
cat dump.sql rules.sql | $ANON --data-only --inserts > anon_dump.sql
```

[pg_dump output options]: https://www.postgresql.org/docs/current/app-pgdump.html#PG-DUMP-OPTIONS


The `RESET search_path` command in `rules.sql` is required because pg_dump
will disable the search_path in `dump.sql` for security reasons.
Alternatively you can use fully-qualified column names in `rules.sql`,
for instance `public.people.lastname` instead of `people.lastname`.


Masking primary keys with Backup Masking
------------------------------------------------------------------------------

Primary keys (such as `SERIAL`) are often masked with the `anon.random_id()`
function which will generate a unique random identifier every it is called.

However this function will not work with Backup Masking because `pg_dump` will *
connect in read-only mode to the database (`default_transaction_read_only=on;`)
and the `anon.random_id()` function needs to update a sequence to avoid
generating the same value twice.

See issue #529 for more details:

<https://gitlab.com/dalibo/postgresql_anonymizer/-/issues/529>

Therefore if you use `anon.random_id() in some rules, the backup masking process
will throw the following error :

```console
pg_dump: detail: Error message from server:
ERROR:  permission denied for sequence random_id_seq
```

The solution is to rewrite the masking rules based on `anon.random_id()` and
use `anon.pseudo_shift(BIGINT)` or `anon.pseudo_xor(BIGINT)` instead.

For instance the masking rule below:

```sql
SECURITY LABEL FOR anon ON COLUMN people.id
  IS 'MASKED WITH FUNCTION anon.random_id()';
```

would become

```sql
SECURITY LABEL FOR anon ON COLUMN people.id
  IS 'MASKED WITH FUNCTION anon.pseudo_xor(id)';
```

The `anon.pseudo_shift(BIGINT)` and `anon.pseudo_xor(BIGINT)` functions use
a secret value (`anon.shift`) to pseudonymize the primary key. The secret value
can be initialized randomly with `anon.set_shift()` or defined with
`anon.set_shift(INT)`.

**WARNING**: Remember that [Pseudonymization is not Anonymization] !

[Pseudonymization is not Anonymization]: https://postgresql-anonymizer.readthedocs.io/en/stable/masking_functions/#pseudonymization


DEPRECATED : pg_dump_anon.sh and pg_dump_anon
------------------------------------------------------------------------------

In version 0.x, the anonymous dumps were done with a shell script named
`pg_dump_anon.sh`. In version 1.x it was done with a golang script named
`pg_dump_anon`. **Both commands are now deprecated.**
