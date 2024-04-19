Anonymous Dumps
===============================================================================


EXPERIMENTAL : Transparent Anonymous Dumps
------------------------------------------------------------------------------

> WARNING: This feature is under development and will not be officially
> supported until version 2.0 is released. Use with care. For a more stable
> solution, see the [pg_dump_anon] section.

To export the anonymized data from a database, follow these 2 steps:

### 1. Create a masked user

```sql
CREATE ROLE dump_anon LOGIN PASSWORD 'x';
ALTER ROLE dump_anon SET anon.transparent_dynamic_masking = True;
SECURITY LABEL FOR anon ON ROLE dump_anon IS 'MASKED';
```

__NOTE:__ You can replace the name `dump_anon` by another name.


### 2. Grant read access to that user

```sql
GRANT USAGE ON SCHEMA public TO dump_anon;
GRANT SELECT ON ALL TABLES IN SCHEMA public TO dump_anon;
GRANT SELECT ON ALL SEQUENCES IN SCHEMA public TO dump_anon;

GRANT USAGE ON SCHEMA foo TO dump_anon;
GRANT SELECT ON ALL TABLES IN SCHEMA foo TO dump_anon;
GRANT SELECT ON ALL SEQUENCES IN SCHEMA foo TO dump_anon;
```

__NOTE:__ Replace `foo` with any other schema you have inside you database.

### 3. Launch pg_dump with the masked user

Now to export the anonymous data from a database named `foo`, let's use
`pg_dump`:

```bash
pg_dump foo \
        --user dump_anon \
        --no-security-labels \
        --extension pgcatalog.plpgsql \
        --file=foo_anonymized.sql
```

__NOTES:__

* linebreaks are here for readability

* `--no-security-labels` will remove the masking rules from the anonymous dump.
  This is really important because masked users should not have access to the
  masking policy.

* `--extension pgcatalog.plpgsql` will remove the `anon` extension, which
  useless inside the anonymized dump. This option is only available with
  `pg_dump 14` and later.

* `--format=custom` is supported


pg_dump_anon
------------------------------------------------------------------------------

The `pg_dump_anon` command support most of the options of the regular [pg_dump]
command. The [PostgreSQL environment variables] ($PGHOST, PGUSER, etc.) and
the [.pgpass] file are also supported.

[PostgreSQL environment variables]: https://www.postgresql.org/docs/current/libpq-envars.html
[.pgpass]: https://www.postgresql.org/docs/current/libpq-pgpass.html


### Example

A user named `bob` can export an anonymous dump of the `app` database like
this:

```bash
pg_dump_anon -h localhost -U bob --password --file=anonymous_dump.sql app
```

**WARNING**: The name of the database must be the last parameter.

For more details about the supported options, simply type `pg_dump_anon --help`


### Install With Go

```console
go install gitlab.com/dalibo/postgresql_anonymizer/pg_dump_anon
```

### Install With docker

If you do not want to instal Go on your production servers, you can fetch the
binary with:

```console
docker run --rm -v "$PWD":/go/bin golang go get gitlab.com/dalibo/postgresql_anonymizer/pg_dump_anon
sudo install pg_dump_anon $(pg_config --bindir)
```



### Limitations

* The user password is asked automatically. This means you must either add
  the `--password` option to define it interactively or declare it in the
  [PGPASSWORD] variable or put it inside the [.pgpass] file ( however on
  Windows,the [PGPASSFILE] variable must be specified explicitly)

* The `plain` format is the only supported format. The other formats (`custom`,
  `dir` and `tar`) are not supported


[PGPASSWORD]: https://www.postgresql.org/docs/current/libpq-envars.html
[PGPASSFILE]: https://www.postgresql.org/docs/current/libpq-envars.html


Obsolete: pg_dump_anon.sh
------------------------------------------------------------------------------

Before version 1.0, `pg_dump_anon` was a bash script. This script was nice and
simple, however under certain conditions the backup were not consistent. See
[issue #266] for more details.

[issue #266]: https://gitlab.com/dalibo/postgresql_anonymizer/-/issues/266

This script is now renamed to `pg_dump_anon.sh` and it is still available for
backwards compatibility. But it will be deprecated in version 2.0.


