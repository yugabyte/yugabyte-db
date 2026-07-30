# pg_duckdb Docker Image

## Available tags

Any tagged release are built and pushed with the tag `$POSTGRES_VERSION-$TAG`, for example:

* `15-v1.1.0`
* `16-v1.1.0`
* `17-v1.1.0`
* `18-v1.1.0`

### Nightly builds

Nightly builds of the `main` branch are available for their respective Postgres versions:

* `15-main`
* `16-main`
* `17-main`
* `18-main`

A corresponding tag with the git sha of the build is also pushed.

If you use a nightly image tag, you will need to pull to receive updates, for example:

```
docker pull pgduckdb/pgduckdb:18-main
```

## Usage instructions

Use of this image is [the same as the Postgres image](https://hub.docker.com/_/postgres/). For example, you can run the image directly:

```shell
docker run -d -e POSTGRES_PASSWORD=duckdb pgduckdb/pgduckdb:18-main
```

And with MotherDuck, it is as simple as:
```shell
$ export MOTHERDUCK_TOKEN=<your personal MD token>
$ docker run -d -e POSTGRES_PASSWORD=duckdb -e MOTHERDUCK_TOKEN pgduckdb/pgduckdb:18-main
```

You can also use docker compose from duckdb/pg_duckdb:

```shell
git clone https://github.com/duckdb/pg_duckdb && cd pg_duckdb && docker compose up -d
```

One started, connect to the database using psql:

```shell
psql postgres://postgres:duckdb@127.0.0.1:5432/postgres
# Or if using docker compose
docker compose exec db psql
```

For usage of pg_duckdb itself, please see [the pg_duckdb README](https://github.com/duckdb/pg_duckdb).
