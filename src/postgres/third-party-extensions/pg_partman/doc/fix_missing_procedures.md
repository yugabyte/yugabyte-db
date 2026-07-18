# Add Missing Procedures to an Upgraded PostgreSQL Instance

If pg_partman was installed on your database instance before it was upgraded to at least PostgreSQL 11, it will likely be missing some or all of the new PROCEDUREs that were added over time. It may have some of them if you have since updated pg_partman to a more recent version, but that only installed PROCEDUREs that happened to be part of that update. There could still be some missing.

The best way to fix this and ensure all PROCEDUREs have been installed is to drop and recreate the extension once you are on PG11 or greater. It is recommended that you test the steps below in development/testing before running on any production systems so you are sure the outcome works as expected.

**IMPORTANT NOTES:** 
 1. If you installed pg_partman originally on PG11 or later, you DO NOT need to do any steps in this guide.
 2. Since the entire extension is being dropped and recreated, you will lose any grants that had been given on any specific extension objects and default privileges that were revoked may be restored. Please make note of the users that were managing partition maintenance before and ensure they have their grants restored.
 3. If you are still using trigger-based partitioning, you will have to take an outage for all trigger-based tables since objects that the triggers use will be dropped and restored. It is highly recommended to migrate away from trigger-based partitioning if possible. This is both for performance reasons as well as future-proofing since trigger-based partitioning may be going away in a future version.
 4. The same version of pg_partman that is dropped **MUST** be reinstalled to restore the configuration. It is recommended that you install the latest version available before starting this update.

## Update Steps

 1. Perform a pg_dump of the data from the pg_partman configuration tables. Note that the contents of this dump will only contain the data and not the table definitions. The definitions are part of the CREATE EXTENSION step. This is just doing a plaintext dump to make it easier to review the contents if desired. Note the following command assumes pg_partman was installed in the `partman` schema.
```
pg_dump -d mydbname -Fp -a -f partman_update_procedures.sql -t partman.part_config -t partman.part_config_sub
```
 2. Drop the pg_partman extension. If it was installed in a specific schema make note of this and reinstall it to that same schema
```
\dx pg_partman
                             List of installed extensions
    Name    | Version | Schema  |                     Description                      
------------+---------+---------+------------------------------------------------------
 pg_partman | 4.7.0   | partman | Extension to manage partitioned tables by time or ID
```
```
DROP EXTENSION pg_partman;
```
 3. Reinstall pg_partman to the same schema
```
CREATE EXTENSION pg_partman SCHEMA partman;
```
 4. Reload the data back into the extension configuration tables
```
psql -d mydbname -i partman_update_procedures.sql
```
 5. Restore privileges to pg_partman objects if needed

You should now have any missing PROCEDUREs available to use as well as your original pg_partman configuration.

