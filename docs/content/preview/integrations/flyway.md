---
title: Flyway
linkTitle: Flyway
description: Using Flyway with YugabyteDB
aliases:
menu:
  preview_integrations:
    identifier: flyway
    parent: schema-migration
    weight: 571
type: docs
---

[Flyway](https://flywaydb.org/) provides the means to manage schema changes to a YugabyteDB database, among others.

The YugabyteDB-specific implementation of the Flyway APIs has been added to the Flyway community project.

## Prerequisites

To use Flyway with YugabyteDB, you need the following:

- YugabyteDB version 2.4 or later (see [Quick Start](../../quick-start/)).

- Flyway community edition version 7.11.2 or later (see [Download Flyway](https://flywaydb.org/download)).

- Connection details in the Flyway configuration file `conf/flyway.conf`. Append the following at the end of the file, replacing `flyway.url` with the URL of your running YugabyteDB cluster:

  ```properties
  flyway.url=jdbc:postgresql://localhost:5433/yugabyte
  flyway.user=yugabyte
  flyway.password=yugabyte
  ```

## Migrate schema

Flyway allows you to specify migrations using either SQL or Java.

{{< note title="Note" >}}

By default, Flyway runs migrations inside a transaction. In case of failures, the transaction is rolled back (see [Flyway Transactions](https://flywaydb.org/documentation/concepts/migrations.html#transactions)). Because YugabyteDB does not currently support DDLs inside a user-initiated transaction (instead, it runs a DDL inside an implicit transaction), you may need to manually revert the DDL changes when you see a message about failed migrations "Please restore backups and roll back database and code".

{{< /note >}}

### Use SQL

You can specify migrations as SQL statements in `.sql` files that are placed in the `<FLYWAY_INSTALL_DIR>/sql/` directory by default. You can change the location of these files by placing them in a different directory and then editing the `flyway.locations` property in the `flyway.conf` file accordingly.

To migrate schema using SQL, perform the following:

- Create migration SQL scripts under the `sql/` directory, as shown in the following example using the `motorcycle_manufacturers` table:

  ```plsql
  cat sql/Create_motorcycle_manufacturers_table.sql
  ```

  ```sql
  CREATE TABLE motorcycle_manufacturers (
    manufacturer_id SERIAL PRIMARY KEY,
    manufacturer_name VARCHAR(50) NOT NULL
  );
  ```

  ```plsql
  cat V2__Insert_into_motorcycle_manufacturers.sql
  ```

  ```sql
  INSERT INTO motorcycle_manufacturers (manufacturer_id, manufacturer_name)
  VALUES (default, 'Harley-Davidson'), (default, 'Yamaha');
  ```

- Run migrations by executing the following command:

  ```shell
   ./flyway migrate
  ```

### Use Java

You can define Flyway migrations as Java classes by extending the `BaseJavaMigration` class and overriding the `migrate()` method.

The following example shows how to use Java to add a column called `hq_address` to the `motorcycle_manufacturers` table:

```java
package db.migration;

import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;
import java.sql.Statement;

public class V3__AddHQAdress extends BaseJavaMigration {

  public void migrate(Context context) throws Exception {
    try (Statement alter = context.getConnection().createStatement()) {
      alter.execute("ALTER TABLE motorcycle_manufacturers
                    ADD COLUMN hq_address VARCHAR(50)");
    }
  }
}
```

After you compile the Java class, place its JAR in the `<FLYWAY_INSTALL_DIR>/jars/` directory, as this is the default location for Flyway to look for Java migration JARs. If you want to use a different location, you need to specify the JAR path by editing the `flyway.locations` property in the `flyway.conf` file.

To check the state of the database and run the migration, execute the following commands:

```shell
 ./flyway info
```

```shell
 ./flyway migrate
```
