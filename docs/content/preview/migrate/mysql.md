
Create a database user and provide the user with READ access to all the resources which need to be migrated. Replace `'localhost'` from the following commands with an appropriate hostname in your setup.

- Create a new user `ybmigrate`.

```sql
CREATE USER 'ybmigrate'@'localhost' IDENTIFIED WITH  mysql_native_password BY 'Password#123';
```

- Grant the global `PROCESS` permission.

```sql
GRANT PROCESS ON *.* TO 'ybmigrate'@'localhost';
```

- Grant the `SELECT`, `SHOW VIEW`, and `TRIGGER` permissions on the source database:

```sql
GRANT SELECT ON source_db_name.* TO 'ybmigrate'@'localhost';
GRANT SHOW VIEW ON source_db_name.* TO 'ybmigrate'@'localhost';
GRANT TRIGGER ON source_db_name.* TO 'ybmigrate'@'localhost';
```

- If you are running MySQL version >= 8.0.20, grant the global `SHOW_ROUTINE` permission. For older versions, grant the global `SELECT` permission. These permissions are necessary to dump stored procedure/function definitions.

```sql
--For MySQL >= 8.0.20
GRANT SHOW_ROUTINE  ON *.* TO 'ybmigrate'@'localhost';
```

```sql
--For older versions
GRANT SELECT ON *.* TO 'ybmigrate'@'localhost';
```

Now you can use the `ybmigrate` user for migration.

- You'll need to provide the user and the source database details in the subsequent invocations of yb_migrate. For convenience, you can populate the information in the following environment variables:

```sh
export SOURCE_DB_TYPE=mysql
export SOURCE_DB_HOST=localhost
export SOURCE_DB_PORT=1521
export SOURCE_DB_USER=ybmigrate
export SOURCE_DB_PASSWORD=password
export SOURCE_DB_NAME=pdb1
export SOURCE_DB_SCHEMA=sakila
```
