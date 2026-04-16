<!--
+++
private=true
+++
-->

Create a database user and provide the user with READ access to all the resources which need to be migrated. Run the following commands in a psql session:

1. Create a new user named `ybvoyager`.

   ```sql
   CREATE USER ybvoyager PASSWORD 'password';
   ```

1. Grant permissions for migration. Use the [yb-voyager-pg-grant-migration-permissions.sql](../../reference/yb-voyager-pg-grant-migration-permissions/) script (in `/opt/yb-voyager/guardrails-scripts/` or, for brew, check in `$(brew --cellar)/yb-voyager@<voyagerversion>/<voyagerversion>`) to grant the required permissions as follows:

   ```sql
   psql -h <host> \
        -d <database> \
        -U <username> \ # A superuser or a privileged user with enough permissions to grant privileges
        -v voyager_user='ybvoyager' \
        -v schema_list='<comma_separated_schema_list>' \
        -v is_live_migration=0 \
        -v is_live_migration_fall_back=0 \
        -f <path_to_the_script>
   ```

   The `ybvoyager` user can now be used for migration.

If you want yb-voyager to connect to the source database over SSL, refer to [SSL Connectivity](../../reference/yb-voyager-cli/#ssl-connectivity).
