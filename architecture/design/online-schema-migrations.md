Tracking issue: [#4192](https://github.com/yugabyte/yugabyte-db/issues/4192)

# Online Schema Migrations

Most applications have a need to constantly evolve the database schema, while simultaneously not being able to take any downtime. Therefore, there is a need for schema migrations (which involve DDL operations) to be safely run concurrently with foreground operations. In case of a failure, the schema change should be rolled back and not leave the database in a partially modified state.

> **NOTE:** A schema migration (or DDL operation) that is performed concurrently with foreground DML operations is henceforth referred to as an *online* schema migration.

# Goals

The goals of this feature are as follows.

* **Safety for online DDL operations**
    * Identify the set of DDL operations (run as a part of schema migrations) that can run safely and in an online manner.
    * Support more DDL operations that can be run safely in an online manner.
    * In the case of DDL statement failures, the DB should revert to the state before the DDL operation started.

* **Consistency of schema for concurrently running transactions**
    * The DB should expose a consistent view of the schema at all points of time, including the period when the change is happening. 
    * While a schema change could be considered a long running process, any concurrently running transaction should execute with a fixed schema for its entire lifetime.

* **Observability**
    * The user should be able to inspect the status of the DDL change including determing when it is completed
    * The user should be able to determine the reason for the failure of the DDL operation.
    * In case of failures, the semantics of rollback should be clear. For example, if an `ADD CONSTRAINT` operation fails, the reason for the failure such as a constraint violation should be stated.

* **General purpose framework**

    Schema migrations can be handled by frameworks or initiated as DDL statements. The framework should work across all of these:

    * DDL statements issued by applications, including initiated by the user from a shell (example: `ysqlsh` or `psql`)
    * Schema migrations performed by ORM frameworks (example: Spring/Hibernate, Django, etc)
    * Standalone database migration software libraries (example: [Flyway](https://flywaydb.org/) and [Liquibase](https://www.liquibase.org/))


> **Note:** There are a number of DDL operations currently supported by YugabyteDB, including many more that are planned in the roadmap. The aim is to increase the coverage for DDL operations that are safe to run in an online manner.


# Motivation

Traditionally, schema migrations were handled by taking all services down during the upgrade. A migration script would run to apply the new schema. Depending on the exact schema change, there could be a step to transform all the data before the schema migration completes. THe services are brought back online after this.

While the approach is very safe, it leads to a lot of downtime in practice, especially in a distributed SQL database like YugabyteDB for the following reasons:

* Simple DDL operations could take longer because the schema change needs to be applied across a cluster of nodes. This is especially so in clusters with a large number of nodes.
* Some DDL operations (such as adding an index or constraint) would need to transform data. Since the data set sizes can be quite large, transforming the data could take a very long time.
* There could be a number of DDL operations that are executed as a part of the schema migration.

The design for online schema migration is based on [the Google F1 design for online, asynchonous schema changes](https://storage.googleapis.com/pub-tools-public-publication-data/pdf/41376.pdf).

# Safe and unsafe DDL Operations

Below is an outline of the various DDL operations that are supported by YSQL and YCQL today (the expectation is that YSQL will end up supporting all the operations eventually even though this is not the case today). The aim is to make all these operations safe for online schema changes. 

| Operation Type | DDL Operation            | YSQL Support | YCQL Support  | Design        |
| :--------: |-------------------------------------------------- |:--:|:--:|:-------------:|
|`COLUMN`    | Add a column                                      | ✅ | ✅ |  |
|`COLUMN`    | Drop an existing column                           | ✅ | ✅ |  |
|`COLUMN`    | Add a column with a default value                 | ✅ | :x:|  |
|`COLUMN`    | Change default value of a column                  | ✅ | :x:|  |
|`COLUMN`    | Add a column that is non-nullable                 | ✅ | :x:|  |
|`COLUMN`    | Change the type of a column                       | :x:| :x:|  |
|`CONSTRAINT`| Add a unique constraint                           | ✅ | ✅ |  |
|`CONSTRAINT`| Add non-unique constraints (CHECK, NOT NULL, etc) | ✅ | :x:|  |
|`CONSTRAINT`| Drop constraint                                   | ✅ | ✅ |  |
|`INDEX`     | Add index (not primark key)                       | ✅ | ✅ |  |
|`INDEX`     | Add primary key                                   | ✅ | ✅ |  |
|`INDEX`     | Alter primary key                                 | ✅ | ✅ |  |
|`INDEX`     | Drop index                                        | ✅ | ✅ |  |
|`TABLE`     | Create table                                      | ✅ | ✅ |  |
|`TABLE`     | Rename table                                      | :x:| :x:|  |
|`TABLE`     | Drop table                                        | ✅ | ✅ |  |

This section first introduces a the generalized framework for performing online schema changes. The subsequent sections will discuss how this framework is used to carry out the specific operations.

# Design - General purpose schema change framework

TODO: fill in

# Handling DDL operations

TODO: fill in


# References

* [Online, Asynchonous Schema Change in F1](https://storage.googleapis.com/pub-tools-public-publication-data/pdf/41376.pdf)
* [Safe and unsafe operations for high volume PostgreSQL](https://leopard.in.ua/2016/09/20/safe-and-unsafe-operations-postgresql)


[![Analytics](https://yugabyte.appspot.com/UA-104956980-4/architecture/design/online-schema-migrations.md?pixel&useReferer)](https://github.com/yugabyte/ga-beacon)
