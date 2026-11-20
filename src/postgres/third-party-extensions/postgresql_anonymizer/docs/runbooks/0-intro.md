---

run-sql:

- dbname: boutique
- user: paul
- parse_query: False
...


# Welcome to Paul's Boutique !

This is a 4 hours workshop that demonstrates various anonymization
techniques using the [PostgreSQL Anonymizer] extension.

[PostgreSQL Anonymizer]: https://labs.dalibo.com/postgresql_anonymizer

## The Story

![Paul's boutique](../images/boutique.jpg)


Paul's boutique has a lot of customers. Paul asks his friend Pierre, a
Data Scientist, to make some statistics about his clients : average age,
etc...

Pierre wants a direct access to the database in order to write SQL
queries.

Jack is an employee of Paul. He's in charge of relationship with the
various suppliers of the shop.

Paul respects his suppliers privacy. He needs to hide the personal
information to Pierre, but Jack needs read and write access the real
data.


## Objectives

Using the simple example above, we will learn:

- How to write masking rules
- The difference between static and dynamic masking
- Implementing advanced masking techniques


## About GDPR

This tutorial **does not** go into the details of the GPDR act and the
general concepts of anonymization.

For more information about it, please refer to the talk below:

- [Anonymisation, Au-delà du RGPD](https://www.youtube.com/watch?v=KGSlp4UygdU) (Video / French)
- [Anonymization, Beyond GDPR](https://public.dalibo.com/exports/conferences/_archives/_2019/20191016_anonymisation_beyond_GDPR/anonymisation_beyond_gdpr.pdf)
    (PDF / english)


## Requirements

In order to make this workshop, you will need:

- A Linux VM ( preferably `Debian 12 bookworm` or `Ubuntu 24.04`)
- A PostgreSQL instance ( preferably `PostgreSQL 17` )
- The PostgreSQL Anonymizer (anon) extension, installed and
    initialized by a superuser
- A database named "boutique" owned by a **superuser** called "paul"
- A role "pierre" and a role "jack", both allowed to connect to
    the database "boutique"

Check out the [INSTALL] section to learn how to install
the [PostgreSQL Anonymizer] extension:

[INSTALL]: https://postgresql-anonymizer.readthedocs.io/en/stable/INSTALL/

A simple way to deploy a workshop environment is to install [Docker Desktop]
and download the image below:


``` console
ANON_IMG=registry.gitlab.com/dalibo/postgresql_anonymizer:stable
docker pull $ANON_IMG
```

And you can then launch it with:

``` console
docker run --name anon_tuto --detach -e POSTGRES_PASSWORD=x $ANON_IMG
docker exec -it anon_tuto psql -U postgres
```

[Docker Desktop]: https://www.docker.com/products/docker-desktop/


!!! tip

    Check out the [INSTALL section](https://postgresql-anonymizer.readthedocs.io/en/stable/INSTALL)
    in the [documentation](https://postgresql-anonymizer.readthedocs.io/en/stable/)
    to learn how to install the extension in your PostgreSQL instance.


## The Roles

We will with 3 different users:

```text { .run-postgres user=postgres dbname=postgres show_result=false }
CREATE ROLE paul LOGIN SUPERUSER PASSWORD 'CHANGEME';

CREATE ROLE pierre LOGIN PASSWORD 'CHANGEME';

CREATE ROLE jack LOGIN PASSWORD 'CHANGEME';
GRANT pg_read_all_data TO jack;
GRANT pg_write_all_data TO jack;

```


Unless stated otherwise, all commands must be executed with the role `paul`.


!!! tip

    Setup a `.pgpass` file to simplify the connections !

```console
cat > ~/.pgpass << EOL
*:*:boutique:paul:CHANGEME
*:*:boutique:pierre:CHANGEME
*:*:boutique:jack:CHANGEME
EOL
chmod 0600 ~/.pgpass
```



## The Sample database

We will work on a database called "boutique":

```text { .run-postgres user=postgres dbname=postgres }
CREATE DATABASE boutique OWNER paul;
```

We need to activate the `anon` library inside that database:

```text { .run-postgres user=postgres dbname=postgres }
ALTER DATABASE boutique
  SET session_preload_libraries = 'anon';
```
