
Create a database user and provide the user with READ access to all the resources which need to be migrated. Replace `'localhost'` from the following commands with an appropriate hostname in your setup.

1. Create a new user `ybvoyager`.

   ```sql
   CREATE USER 'ybvoyager'@'localhost' IDENTIFIED WITH  mysql_native_password BY 'Password#123';
   ```

1. Grant the global `PROCESS` permission.

   ```sql
   GRANT PROCESS ON *.* TO 'ybvoyager'@'localhost';
   ```

1. Grant the `SELECT`, `SHOW VIEW`, and `TRIGGER` permissions on the source database:

   ```sql
   GRANT SELECT ON source_db_name.* TO 'ybvoyager'@'localhost';
   GRANT SHOW VIEW ON source_db_name.* TO 'ybvoyager'@'localhost';
   GRANT TRIGGER ON source_db_name.* TO 'ybvoyager'@'localhost';
   ```

1. If you are running MySQL version >= 8.0.20, grant the global `SHOW_ROUTINE` permission. For older versions, grant the global `SELECT` permission. These permissions are necessary to dump stored procedure/function definitions.

   ```sql
   --For MySQL >= 8.0.20
   GRANT SHOW_ROUTINE  ON *.* TO 'ybvoyager'@'localhost';
   ```

   ```sql
   --For older versions
   GRANT SELECT ON *.* TO 'ybvoyager'@'localhost';
   ```

   The `ybvoyager` user can now be used for migration.
