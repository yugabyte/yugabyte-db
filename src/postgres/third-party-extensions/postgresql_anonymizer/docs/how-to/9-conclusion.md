# Conclusion

----

## Clean up !



``` { .run-postgres user=postgres }
DROP EXTENSION anon CASCADE;
```

----

``` { .run-postgres user=postgres dbname=postgres }

REASSIGN OWNED BY jack TO postgres;
REVOKE ALL ON SCHEMA public FROM jack;

REASSIGN OWNED BY paul TO postgres;

REASSIGN OWNED BY pierre TO postgres;
```

----


<!---
We can't use run-postgres here, because the pandoc-run-postgres filter is
already connected to the "boutique" database. Therefore we can't remove it at
this point of the document generation
-->

``` { .run-postgres user=postgres dbname=postgres }
DROP DATABASE IF EXISTS boutique;
```

----


``` { .run-postgres user=postgres dbname=postgres }
DROP ROLE IF EXISTS jack;
DROP ROLE IF EXISTS paul;
DROP ROLE IF EXISTS pierre;
```

----

## Many Masking Strategies

-   [Static
    Masking](https://postgresql-anonymizer.readthedocs.io/en/stable/static_masking/)
    : perfect for \"once-and-for-all\" anonymization
-   [Dynamic
    Masking](https://postgresql-anonymizer.readthedocs.io/en/stable/dynamic_masking/)
    : useful when one user is untrusted
-   [Anonymous
    Dumps](https://postgresql-anonymizer.readthedocs.io/en/stable/anonymous_dumps/)
    : can be used in CI/CD workflows
-   [Generalization](https://postgresql-anonymizer.readthedocs.io/en/stable/generalization/)
    : good for statistics and data science

----

## Many Masking Functions

-   Destruction and partial destruction
-   Adding Noise
-   Randomization
-   Faking and Advanced Faking
-   Pseudonymization
-   Generic Hashing
-   Custom masking

RTFM -\> [Masking
Functions](https://postgresql-anonymizer.readthedocs.io/en/latest/masking_functions/)

## Advantages

-   Masking rules written in SQL
-   Masking rules stored in the database schema
-   No need for an external ETL
-   Works with all current versions of PostgreSQL
-   Multiple strategies, multiple functions

## Drawbacks

-   Does not work with other databases (hence the name)
-   Lack of feedback for huge tables (\> 10 TB)

## Also...

Other projects you may like

-   [pg_sample](https://github.com/mla/pg_sample) : extract a small
    dataset from a larger PostgreSQL database
-   [PostgreSQL Faker](https://gitlab.com/dalibo/postgresql_faker) : An
    advanced faking extension based on the python Faker lib

## Help Wanted!

This is a free and open project!

[labs.dalibo.com/postgresql_anonymizer](https://labs.dalibo.com/postgresql_anonymizer)

Please send us feedback on how you use it, how it fits your needs (or
not), etc.

## This is a 4 hour workshop!

Sources are here:
[gitlab.com/dalibo/postgresql_anonymizer](https://gitlab.com/dalibo/postgresql_anonymizer/-/tree/master/docs/how-to)

Download the [PDF
Handout](https://dalibo.gitlab.io/postgresql_anonymizer/how-to.handout.pdf)

## Questions?

![](./img/deal_with_it.gif)
:::

::: {.cell .code}
``` python
```
:::
