
Conclusion
===============================================================================

----

Clean up !
-------------------------------------------------------------------------------

``` { .run-postgres user=postgres dbname=postgres }
DROP DATABASE IF EXISTS boutique;
```


``` { .run-postgres user=postgres dbname=postgres }

REASSIGN OWNED BY jack TO postgres;

REASSIGN OWNED BY paul TO postgres;

REASSIGN OWNED BY pierre TO postgres;
```



``` { .run-postgres user=postgres dbname=postgres }
DROP ROLE IF EXISTS jack;
DROP ROLE IF EXISTS paul;
DROP ROLE IF EXISTS pierre;
DROP ROLE IF EXISTS dump_anon;
```

Also...
-------------------------------------------------------------------------------

Other projects you may like

- [pg_sample](https://github.com/mla/pg_sample) : extract a small
  dataset from a larger PostgreSQL database

Help Wanted!
-------------------------------------------------------------------------------

This is a free and open project!

[labs.dalibo.com/postgresql_anonymizer](https://labs.dalibo.com/postgresql_anonymizer)

Please send us feedback on how you use it, how it fits your needs (or
not), etc.
