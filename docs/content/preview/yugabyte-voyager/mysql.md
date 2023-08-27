<!--
+++
private=true
+++
-->

Create a database user and provide the user with READ access to all the resources which need to be migrated. Replace `<client_IP>` from the following commands with an appropriate hostname in your setup.

1. Create a new user `ybvoyager`.

   ```sql
   CREATE USER 'ybvoyager'@'<client_IP>' IDENTIFIED WITH  mysql_native_password BY 'Password#123';
   ```

1. Grant the global `PROCESS` permission.

   ```sql
   GRANT PROCESS ON *.* TO 'ybvoyager'@'<client_IP>';
   ```

1. Grant the `SELECT`, `SHOW VIEW`, and `TRIGGER` permissions on the source database:

   ```sql
   GRANT SELECT ON source_db_name.* TO 'ybvoyager'@'<client_IP>';
   GRANT SHOW VIEW ON source_db_name.* TO 'ybvoyager'@'<client_IP>';
   GRANT TRIGGER ON source_db_name.* TO 'ybvoyager'@'<client_IP>';
   ```

   Note that if you want to [accelerate data export](#accelerate-data-export-for-mysql-and-oracle), include the following grants additionally as follows:

   **For MYSQL**

   ```sql
   GRANT FLUSH_TABLES ON *.* TO 'ybvoyager'@'<client_IP>';
   GRANT REPLICATION CLIENT ON *.* TO 'ybvoyager'@'<client_IP>';
   ```

   **For MYSQL RDS**

   ```sql
   GRANT FLUSH_TABLES ON *.* TO 'ybvoyager'@'<client_IP>';
   GRANT REPLICATION CLIENT ON *.* TO 'ybvoyager'@'<client_IP>';
   GRANT LOCK TABLES ON <source_db_name>.* TO 'ybvoyager'@'<client_IP>';
   ```

1. If you are running MySQL version 8.0.20 or later, grant the global `SHOW_ROUTINE` permission. For older versions, grant the global `SELECT` permission. These permissions are necessary to dump stored procedure/function definitions.

   ```sql
   --For MySQL >= 8.0.20
   GRANT SHOW_ROUTINE  ON *.* TO 'ybvoyager'@'<client_IP>';
   ```

   ```sql
   --For older versions
   GRANT SELECT ON *.* TO 'ybvoyager'@'<client_IP>';
   ```

   The `ybvoyager` user can now be used for migration.
