PostgreSQL Partition Manager Extension (pg_partman)
===================================================

About
-----
pg_partman is an extension to help make managing time or serial id based table partitioning easier. Currenly the trigger functions only handle inserts to the parent table. Updates that would move a value from one partition to another are not yet supported.

For this extension, most of the attributes of the child partitions are all obtained from the original parent. This includes defaults, indexes (primary keys, unique, etc), tablespace, constraints, privileges & ownership. For managing privileges, whenever a new partition is created it will obtain its privilege & ownership information from what the parent has at that time. Previous partition privileges are not changed. If previous partitions require that their privileges be updated, a separate function is available. This is kept as a separate process due to being an expensive operation when the partition set grows larger. The defaults, indexes, tablespace & constraints on the parent are only applied to newly created partitions and are not retroactively set on ones that already existed. While you would not normally create indexes on the parent of a partition set, doing so makes it much easier to manage in this case. There will be no data in the parent table (if everything is working right), so they will not take up any space or have any impact on system performance. Using the parent table as a control to the details of the child tables like this gives a better place to manage things that's a little more natural than a configuration table or using setup functions.

If you attempt to insert data into a partition set that contains data for a partition that does not exist, that data will be placed into the set's parent table. This is preferred over automatically creating new partitions to match that data since a mistake that is causing non-partitioned data to be inserted could cause a lot of unwanted child tables to be made. The check_parent() function provides monitoring for any data getting inserted into parents and the partition_data_* set of functions can easily partition that data for you if it is valid data. That is much easier than having to clean up potentially hundreds or thousands of unwanted partitions. And also better than throwing an error and losing the data!

If you don't need to keep data in older partitions, a retention system is available to automatically drop unneeded child partitions. By default, they are only uninherited not actually dropped, but that can be configured if desired. If the old partitions are kept, dropping their indexes can also be configured to recover disk space. Note that this will also remove any primary key or unique constraints in order to allow the indexes to be dropped. There is also a method available to dump the tables out if they don't need to be in the database anymore but still need to be kept. To set the retention policy, enter either an interval or integer value into the **retention** column of the **part_config** table. For time-based partitioning, the interval value will set that any partitions containing only data older than that will be dropped. For id-based partitioning, the integer value will set that any partitions with an id value less than the current maximum id value minus the retention value will be dropped. For example, if the current max id is 100 and the retention value is 30, any partitions with id values less than 70 will be dropped. The current maximum id value at the time the drop function is run is always used.

PostgreSQL has an object naming length limit of 63 characters. If you try and create an object with a longer name, it truncates off any characters at the end to fit that limit. This can cause obvious issues with partition names that rely on having a specifically named suffix. PG Partman automatically handles this for all child tables, trigger functions and triggers. It will truncate off the exiting parent table name to fit the required suffix. Be aware that if you have tables with very long, similar names, you may run into naming conflicts if they are part of separate partition sets. With serial based partitioning, be aware that over time the table name will be truncated more an more to fit a longer partition suffix. So while the extention will try and handle this edge case for you, it is recommended to keep table names that will be partitioned as short as possible.

The PG Jobmon extension (https://github.com/omniti-labs/pg_jobmon) is optional and allows auditing and monitoring of partition maintenance. If jobmon is installed and configured properly, it will automatically be used by partman with no additional setup needed. By default, any function that fails to run successfully 3 consecutive times will cause jobmon to raise an alert. This is why the default pre-make value is set to 4 so that an alert will be raised in time for intervention with no additional configuration of jobmon needed. You can of course configure jobmon to alert before (or later) than 3 failures if needed. If you're running partman in a production environment it is HIGHLY recommended to have jobmon installed and some sort of 3rd-party monitoring configured with it to alert when partitioning fails (Nagios, Circonus, etc).

Functions
---------
A superuser must be used to run these functions in order to set privileges & ownership properly in all cases. All are set with SECURITY DEFINER, so if you cannot have a superuser running them just assign a superuser role as the owner. 

*create_parent(p_parent_table text, p_control text, p_type text, p_interval text, p_premake int DEFAULT 4, p_debug boolean DEFAULT false)*
 * Main function to create a partition set with one parent table and inherited children. Parent table must already exist. Please apply all defaults, indexes, constraints, privileges & ownership to parent table so they will propagate to children.
 * An ACCESS EXCLUSIVE lock is taken on the parent table during the running of this function. No data is moved when running this function, so lock should be brief.
 * p_parent_table - the existing parent table. MUST be schema qualified, even if in public schema.
 * p_control - the column that the partitioning will be based on. Must be a time or integer based column.
 * p_type - one of 4 values to set the partitioning type that will be used
 
 > **time-static** - Trigger function inserts only into specifically named partitions. The the number of partitions managed behind and ahead of the current one is determined by the **premake** config value (default of 4 means data for 4 previous and 4 future partitions are handled automatically). *Beware setting the premake value too high as that will lessen the efficiency of this partitioning method.* Inserts to parent table outside the hard-coded time window will go to the parent. Trigger function is kept up to date by run_maintenance() function. Ideal for high TPS tables that get inserts of new data only.  
 > **time-dynamic** - Trigger function can insert into any existing child partition based on the value of the control column at the time of insertion. More flexible but not as efficient as time-static.   
 > **id-static** - Same functionality and use of the premake value as time-static but for a numeric range instead of time. When the id value reaches 50% of the max value for that partition, it will automatically create the next partition in sequence if it doesn't yet exist. Does NOT require run_maintenance() function to create new partitions. Only supports id values greater than or equal to zero.  
 > **id-dynamic** - Same functionality and limitations as time-dynamic but for a numeric range instead of time. Uses same 50% rule as id-static to create future partitions. Does NOT require run_maintenance() function to create new partitions. Only supports id values greater than or equal to zero.

 * p_interval - the time or numeric range interval for each partition. Supported values are:

 > **yearly** - One partition per year  
 > **quarterly** - One partition per yearly quarter. Partitions are named as YYYYqQ (ex: 2012q4)  
 > **monthly** - One partition per month  
 > **weekly** - One partition per week. Follows ISO week date format (http://en.wikipedia.org/wiki/ISO_week_date). Partitions are named as IYYYwIW (ex: 2012w36)  
 > **daily** - One partition per day  
 > **hourly** - One partition per hour  
 > **half-hour** - One partition per 30 minute interval on the half-hour (1200, 1230)  
 > **quarter-hour** - One partition per 15 minute interval on the quarter-hour (1200, 1215, 1230, 1245)  
 > **<integer>** - For ID based partitions, the integer value range of the ID that should be set per partition. This is the actual integer value, not text values like time-based partitioning. Must be greater than zero.  
 > *Author's Note: If people want decade, century or millenium let me know. They are not trivial to add but I will upon request.*

 * p_premake - is how many additional partitions to always stay ahead of the current partition. Default value is 4. This will keep at minimum 5 partitions made, including the current one. For example, if today was Sept 6, 2012, and premake was set to 4 for a daily partition, then partitions would be made for the 6th as well as the 7th, 8th, 9th and 10th. As stated above, this value also determines how many partitions outside of the current one the static partitioning trigger function will handle (behind & ahead). Note that weekly partitioning may occasionally cause an extra partition to be premade due to differing month lengths and daylight savings (on non-UTC systems). This won't hurt anything and will self-correct. If partitioning ever falls behind the premake value, normal running of run_maintenance() and data insertion to id-based tables should automatically catch things up.
 * p_debug - turns on additional debugging information (not yet working).

*run_maintenance()*
 * Run this function as a scheduled job (cronjob, etc) to automatically keep time-based partitioning working and/or use the partition retention system.
 * Every run checks all tables listed in the **part_config** table with the types **time-static** and **time-dynamic** to pre-make child tables (not needed for id-based partitioning).
 * Every run checks all tables of all types listed in the **part_config** table with a value in the **retention** column and drops tables as needed (see **About** and config table below).
 * Will automatically update the function for **time-static** partitioning to keep the parent table pointing at the correct partitions. When using time-static, run this function more often than the partitioning interval to keep the function running its most efficient. For example, if using quarter-hour, run every 5 minutes; if using daily, run at least twice a day, etc.

*partition_data_time(p_parent_table text, p_batch_interval interval DEFAULT NULL, p_batch_count int DEFAULT 1) RETURNS bigint*
 * This function is used to partition data that may have existed prior to setting up the parent table as a time-based partition set, or to fix data that accidentally gets inserted into the parent.
 * If the needed partition does not exist, it will automatically be created. If the needed partition already exists, the data will be moved there.
 * If you are trying to partition a large amount of previous data automatically, it is recommended to run this function with an external script and appropriate batch settings. This will help avoid transactional locks and prevent a failure from causing an extensive rollback. See **Extras** section for an included python script that will do this for you.
 * p_parent_table - the existing parent table. This is assumed to be where the unpartitioned data is located. MUST be schema qualified, even if in public schema.
 * p_batch_interval - optional argument, a time interval of how much of the data to move. This can be smaller than the partition interval, allowing for very large sized partitions to be broken up into smaller commit batches. Defaults to the configured partition interval if not given or if you give an interval larger than the partition interval.
 * p_batch_count - optional argument, how many times to run the batch_interval in a single call of this function. Default value is 1.
 * Returns the number of rows that were moved from the parent table to partitions. Returns zero when parent table is empty and partitioning is complete.

*partition_data_id(p_parent_table text, p_batch_interval int DEFAULT NULL, p_batch_count int DEFAULT 1) RETURNS bigint*
 * This function is used to partition data that may have existed prior to setting up the parent table as a serial id partition set, or to fix data that accidentally gets inserted into the parent.
 * If the needed partition does not exist, it will automatically be created. If the needed partition already exists, the data will be moved there.
 * If you are trying to partition a large amount of previous data automatically, it is recommended to run this function with an external script and appropriate batch settings. This will help avoid transactional locks and prevent a failure from causing an extensive rollback. See **Extras** section for an included python script that will do this for you.
 * p_parent_table - the existing parent table. This is assumed to be where the unpartitioned data is located. MUST be schema qualified, even if in public schema.
 * p_batch_interval - optional argument, an integer amount representing an interval of how much of the data to move. This can be smaller than the partition interval, allowing for very large sized partitions to be broken up into smaller commit batches. Defaults to the configured partition interval if not given or if you give an interval larger than the partition interval.
 * p_batch_count - optional argument, how many times to run the batch_interval in a single call of this function. Default value is 1.
 * Returns the number of rows that were moved from the parent table to partitions. Returns zero when parent table is empty and partitioning is complete.

*undo_partition_time(p_parent_table text, p_batch_count int DEFAULT 1, p_batch_interval interval DEFAULT NULL, p_keep_table boolean DEFAULT true) RETURNS bigint*
 * Undo a time-based partition set created by pg_partman. This function MOVES the data from existing child partitions to the parent table.
 * When this function is run, the trigger on the parent table & the trigger function are immediately dropped (if they still exist). This means any further writes are done to the parent.
 * When this function is run, the **undo_in_progress** column in the configuration table is set. This causes all partition creation and retention management by the run_maintenance() function to stop.
 * If you are trying to un-partition a large amount of data automatically, it is recommended to run this function with an external script and appropriate batch settings. This will help avoid transactional locks and prevent a failure from causing an extensive rollback. See **Extras** section for an included python script that will do this for you.
 * By default, partitions are not DROPPED, they are UNINHERITED. This leave previous child tables as empty, independent tables.
 * Without setting either batch argument manually, each run of the function will move all the data from a single partition into the parent.
 * Once all child tables have been uninherited/dropped, the configuration data is removed from pg_partman automatically.
 * p_parent_table - parent table of the partition set. Must be schema qualified and match a parent table name already configured in pg_partman.
 * p_batch_count - an optional argument, this sets how many times to move the amount of data equal to the p_batch_interval argument (or default partition interval if not set) in a single run of the function. Defaults to 1.
 * p_batch_interval - an optional argument, a time interval of how much of the data to move. This can be smaller than the partition interval, allowing for very large sized partitions to be broken up into smaller commit batches. Defaults to the configured partition interval if not given or if you give an interval larger than the partition interval.
 * p_keep_table - an optional argument, setting this to false will cause the old child table to be dropped instead of uninherited after all of it's data has been moved. Note that it takes at least two batches to actually ininherit/drop a table from the set.
 * Returns the number of rows moved to the parent table. Returns zero when child tables are all empty.

*undo_partition_id(p_parent_table text, p_batch_count int DEFAULT 1, p_batch_interval bigint DEFAULT NULL, p_keep_table boolean DEFAULT true) RETURNS bigint*
 * Undo an id-based partition set created by pg_partman. This function MOVES the data from existing child partitions to the parent table.
 * When this function is run, the trigger on the parent table & the trigger function are immediately dropped (if they still exist). This means any further writes are done to the parent.
 * When this function is run, the **undo_in_progress** column in the configuration table is set. This causes all partition creation and retention management by the run_maintenance() function to stop.
 * If you are trying to un-partition a large amount of data automatically, it is recommended to run this function with an external script and appropriate batch settings. This will help avoid transactional locks and prevent a failure from causing an extensive rollback. See **Extras** section for an included python script that will do this for you.
 * By default, partitions are not DROPPED, they are UNINHERITED. This leave previous child tables as empty, independent tables.
 * Without setting either batch argument manually, each run of the function will move all the data from a single partition into the parent.
 * Once all child tables have been uninherited/dropped, the configuration data is removed from pg_partman automatically.
 * p_parent_table - parent table of the partition set. Must be schema qualified and match a parent table name already configured in pg_partman.
 * p_batch_count - an optional argument, this sets how many times to move the amount of data equal to the p_batch_interval argument (or default partition interval if not set) in a single run of the function. Defaults to 1.
 * p_batch_interval - an optional argument, an integer amount representing an interval of how much of the data to move. This can be smaller than the partition interval, allowing for very large sized partitions to be broken up into smaller commit batches. Defaults to the configured partition interval if not given or if you give an interval larger than the partition interval.
 * p_keep_table - an optional argument, setting this to false will cause the old child table to be dropped instead of uninherited after all of it's data has been moved. Note that it takes at least two batches to actually ininherit/drop a table from the set (second batch sees it has no more data and drops it).
 * Returns the number of rows moved to the parent table. Returns zero when child tables are all empty.

*undo_partition(p_parent_table text, p_batch_count int DEFAULT 1, p_keep_table boolean DEFAULT true) RETURNS bigint*
 * Undo the parent/child table inheritance of any partition set, not just ones managed by pg_partman. This function COPIES the data from existing child partitions to the parent table.
 * If you need to keep the data in your child tables after it is put into the parent, use this function. 
 * Unlike the other undo functions, data cannot be copied in batches smaller than the partition interval. Every run of the function copies an entire partition to the parent.
 * If you are trying to un-partition a large amount of data automatically, it is recommended to run this function with an external script and appropriate batch settings. This will help avoid transactional locks and prevent a failure from causing an extensive rollback. See **Extras** section for an included python script that will do this for you.
 * By default, partitions are not DROPPED, they are UNINHERITED. This leave previous child tables exactly as they were but no longer inherited from the parent.
 * p_parent_table - parent table of the partition set. Must be schema qualified but does NOT have to be managed by pg_partman.
 * p_batch_count - an optional argument, this sets how many partitions to copy data from in a single run. Defaults to 1.
 * p_keep_table - an optional argument, setting this to false will cause the old child table to be dropped instead of uninherited. 
 * Returns the number of rows moved to the parent table. Returns zero when child tables are all empty.

*check_parent()*
 * Run this function to monitor that the parent tables of the partition sets that pg_partman manages do not get rows inserted to them.
 * Returns a row for each parent table along with the number of rows it contains. Returns zero rows if none found.

*reapply_privileges(p_parent_table text)*
 * This function is used to reapply ownership & grants on all child tables based on what the parent table has set. 
 * Privileges that the parent table has will be granted to all child tables and privilges that the parent does not have will be revoked (with CASCADE).
 * Privilges that are checked for are SELECT, INSERT, UPDATE, DELETE, TRUNCATE, REFERENCES, & TRIGGER.
 * Be aware that for large partition sets, this can be a very long running operation and is why it was made into a separate function to run independently. Only privileges that are different between the parent & child are applied, but it still has to do system catalog lookups and comparisons for every single child partition and all individual privileges on each.
 * p_parent_table - parent table of the partition set. Must be schema qualified and match a parent table name already configured in pg_partman.

*check_unique_column(p_parent_table text, p_column text)* 
 * Partitioning using inheritance has the shortcoming of not allowing a unique constraint to apply to all tables in the entire partition set without causing large performance issues once the partition set begins to grow very large. This is the first draft of a function to provide a way to monitor if this occurs.
 * Note that on very large partition sets this can be an expensive query to run, especially if you have no index on it (which you should if it was supposed to be unique anyway).
 * p_parent_table - the existing parent table of any partition set.
 * p_column - the column in the partition set to check for uniqueness across all child tables.
 * If there is a column value that violates the unique constraint, this function will return that column value along with a count of how many of that value there are. 

*drop_partition_time(p_parent_table text, p_retention interval DEFAULT NULL, p_keep_table boolean DEFAULT NULL, p_keep_index boolean DEFAULT NULL, p_retention_schema text DEFAULT NULL) RETURNS int
 * This function is used to drop child tables from a time-based partition set. By default, the table is just uninherited and not actually dropped. For automatically dropping old tables, it is recommended to use the **run_maintenance()** function with retention configured instead of calling this directly.
 * p_parent_table - the existing parent table of a time-based partition set. MUST be schema qualified, even if in public schema.
 * p_retention - optional parameter to give a retention time interval and immediately drop tables containing only data older than the given interval. If you have a retention value set in the config table already, the function will use that, otherwise this will override it. If not, this parameter is required. See the **About** section above for more information on retention settings.
 * p_keep_table - optional parameter to tell partman whether to keep or drop the table in addition to uninheriting it. TRUE means the table will not actually be dropped; FALSE means the table will be dropped. This function will just use the value configured in **part_config** if not explicitly set. This option is ignored if retention_schema is set.
 * p_keep_index - optional parameter to tell partman whether to keep or drop the indexes of the child table when it is uninherited. TRUE means the indexes will be kept; FALSE means all indexes will be dropped. This function will just use the value configured in **part_config** if not explicitly set. This option is ignored if p_keep_table is set to FALSE or if retention_schema is set.
 * p_retention_schema - optional parameter to tell partman to move a table to another schema instead of dropping it. Set this to the schema you want the table moved to. This function will just use the value configured in **part_config** if not explicitly set. If this option is set, the retention_keep_* parameters are ignored.
 * Returns the number of partitions affected.

*drop_partition_id(p_parent_table text, p_retention bigint DEFAULT NULL, p_keep_table boolean DEFAULT NULL, p_keep_index boolean DEFAULT NULL, p_retention_schema text DEFAULT NULL) RETURNS int*
 * This function is used to drop child tables from an id-based partition set. By default, the table just uninherited and not actually dropped. For automatically dropping old tables, it is recommended to use the **run_maintenance()** function with retention configured instead of calling this directly.
 * p_parent_table - the existing parent table of a time-based partition set. MUST be schema qualified, even if in public schema.
 * p_retention - optional parameter to give a retention integer interval and immediately drop tables containing only data less than the current maximum id value minus the given retention value. If you have a retention value set in the config table already, the function will use that, otherwise this will override it. If not, this parameter is required. See the **About** section above for more information on retention settings.
 * p_keep_table - optional parameter to tell partman whether to keep or drop the table in addition to uninheriting it. TRUE means the table will not actually be dropped; FALSE means the table will be dropped. This function will just use the value configured in **part_config** if not explicitly set. This option is ignored if retention_schema is set.
 * p_keep_index - optional parameter to tell partman whether to keep or drop the indexes of the child table when it is uninherited. TRUE means the indexes will be kept; FALSE means all indexes will be dropped. This function will just use the value configured in **part_config** if not explicitly set. This option is ignored if p_keep_table is set to FALSE or if retention_schema is set.
 * p_retention_schema - optional parameter to tell partman to move a table to another schema instead of dropping it. Set this to the schema you want the table moved to. This function will just use the value configured in **part_config** if not explicitly set. If this option is set, the retention_keep_* parameters are ignored.
 * Returns the number of partitions affected.

Tables
------
*part_config*  
    Stores all configuration data for partition sets mananged by the extension. The only columns in this table that should ever need to be manually changed are
    **retention**, **retention_schema**, **retention_keep_table**, & **retention_keep_index** to configure the partition set's retention policy or **premake** to change the default.   
    The rest are managed by the extension itself and should not be changed unless absolutely necessary.

    parent_table            - Parent table of the partition set
    type                    - Type of partitioning. Must be one of the 4 types mentioned above.
    part_interval           - Text type value that determines the interval for each partition. 
                              Must be a value that can either be cast to the interval or bigint data types.
    control                 - Column used as the control for partition constraints. Must be a time or integer based column.
    premake                 - How many partitions to keep pre-made ahead of the current partition. Default is 4.
                              Also manages number of partitions handled by static partitioning method. See create_parent() function for more info.
    retention               - Text type value that determines how old the data in a child partition can be before it is dropped. 
                              Must be a value that can either be cast to the interval or bigint data types. 
                              Leave this column NULL (the default) to always keep all child partitions. See **About** section for more info.
    retention_schema        - Schema to move tables to as part of the retentions system instead of dropping them. Overrides retention_keep_* options.
    retention_keep_table    - Boolean value to determine whether dropped child tables are kept or actually dropped. 
                              Default is TRUE to keep the table and only uninherit it. Set to FALSE to have the child tables removed from the database completely.
    retention_keep_index    - Boolean value to determine whether indexes are dropped for child tables that are uninherited. 
                              Default is TRUE. Set to FALSE to have the child table's indexes dropped when it is uninherited.
    datetime_string         - For time-based partitioning, this is the datetime format string used when naming child partitions. 
    last_partition          - Tracks the last successfully created partition and used to determine the next one.
    undo_in_progress        - Set by the undo_partition functions whenever they are run. If true, this causes all partition creation 
                              and retention management by the run_maintenance() function to stop. Default is false.

Extras
------
*partition_data.py*
 * A python script to make partitioning in committed batches easier.
 * Calls either partition_data_time() or partition_data_id depending on the value given for --type.
 * A commit is done at the end of each --interval and/or fully created partition.
 * Returns the total number of rows moved to partitions. Automatically stops when parent is empty.
 * --parent (-p):          Parent table an already created partition set. Required.
 * --type (-t):            Type of partitioning. Valid values are "time" and "id". Required.
 * --connection (-c):      Connection string for use by psycopg to connect to your database. Defaults to "host=localhost". Highly recommended to use .pgpass file or environment variables to keep credentials secure.
 * --interval (-i):        Value that is passed on to the partitioning function as p_batch_interval argument. Use this to set an interval smaller than the partition interval to commit data in smaller batches. Defaults to the partition interval if not given.
 * --batch (-b):           How many times to loop through the value given for --interval. If --interval not set, will use default partition interval and make at most -b partition(s). Script commits at the end of each individual batch. (NOT passed as p_batch_count to partitioning function). If not set, all data in the parent table will be partitioned in a single run of the script.
 * --wait (-w):            Cause the script to pause for a given number of seconds between commits (batches).
 * --lockwait (-l):        Have a lock timeout of this many seconds on the data move. If a lock is not obtained, that batch will be tried again.
 * --lockwait_tries        Number of times to allow a lockwait to time out before giving up on the partitioning. Defaults to 10.
 * --quiet (-q):           Switch setting to stop all output during and after partitioning.
 * Examples:
 > Partition all data in a parent table. Commit after each partition is made.\n
 >       python partition_data.py -c "host=localhost dbname=mydb" -p schema.parent_table -t time\n
 > Partition by id in smaller intervals and pause between them for 5 seconds (assume >100 partition interval)\n
 >       python partition_data.py -p schema.parent_table -t id -i 100 -w 5\n
 > Partition by time in smaller intervals for at most 10 partitions in a single run (assume monthly partition interval)\n
 >       python partition_data.py -p schema.parent_table -t time -i "1 week" -b 10


*undo_partition.py*
 * A python script to make undoing partitions in committed batches easier. 
 * Can also work on any parent/child partition set not managed by pg_partman if --type option is not set.
 * This script calls either undo_partition(), undo_partition_time() or undo_partition_id depending on the value given for --type.
 * A commit is done at the end of each --interval and/or emptied partition.
 * Returns the total number of rows put into the to parent. Automatically stops when last child table is empty.
 * --parent (-p):          Parent table of the partition set. Required.
 * --type (-t):            Type of partitioning. Valid values are "time" and "id". Not setting this argument will use undo_partition() and work on any parent/child table set.
 * --connection (-c):      Connection string for use by psycopg to connect to your database. Defaults to "host=localhost". Highly recommended to use .pgpass file or environment variables to keep credentials secure.
 * --interval (-i):        Value that is passed on to the partitioning function as p_batch_interval. Use this to set an interval smaller than the partition interval to commit data in smaller batches. Defaults to the partition interval if not given.
 * --batch (-b):           How many times to loop through the value given for --interval. If --interval not set, will use default partition interval and undo at most -b partition(s). Script commits at the end of each individual batch. (NOT passed as p_batch_count to undo function). If not set, all data will be moved to the parent table in a single run of the script.
 * --wait (-w):            Cause the script to pause for a given number of seconds between commits (batches).
 * --droptable (-d):       Switch setting for whether to drop child tables when they are empty. Leave off option to just uninherit.
 * --quiet (-q):           Switch setting to stop all output during and after partitioning undo.

*dump_partition.py*
 * A python script to dump out tables contained in the given schema. Uses pg_dump, creates a SHA-512, and then drops the table.
 * When combined with the retention_schema configuration option, provides a way to reliably dump out tables that would normally just be dropped by the retention system.
 * Tables are not dropped if pg_dump does not return successfully.
 * The connection options for psyocpg and pg_dump were separated out due to distinct differences in their requirements depending on your database connection configuration. 
 * All dump_* option defaults are the same as they would be for pg_dump if they are not given.
 * Will work on any given schema, not just the one used to manage pg_partman retention.
 * --schema (-n):          The schema that contains the tables that will be dumped. (Required).
 * --connection (-c):      Connection string for use by psycopg. Must be able to select pg_catalog.pg_tables in the relevant database and drop all tables in the given schema. Defaults to "host=localhost". Note this is distinct from the parameters sent to pg_dump.
 * --output (-o):          Path to dump file output location. Default is where the script is run from.
 * --dump_database (-d):   Used for pg_dump, same as its final database name parameter.
 * --dump_host:       Used for pg_dump, same as its --host option.
 * --dump_username:   Used for pg_dump, same as its --username option.
 * --dump_port:       Used for pg_dump, same as its --port option.
 * --pg_dump_path:         Path to pg_dump binary location. Must set if not in current PATH.
 * --Fp:                   Dump using pg_dump plain text format. Default is binary custom (-Fc).
 * --nohashfile:           Do NOT create a separate file with the SHA-512 hash of the dump. If dump files are very large, hash generation can possibly take a long time.
 * --nodrop:               Do NOT drop the tables from the given schema after dumping/hashing.
 * --verbose (-v):         Provide more verbose output.

*reapply_indexes.py*
 * A python script for reapplying indexes on child tables in a partition set after they are changed on the parent table. 
 * All indexes on all child tables (not including primary key unless specified) will be dropped and recreated for the given set.
 * Commits are done after each index is dropped/created to help prevent long running transactions & locks.
 * WARNING: The default, postgres generated index name is used for all children when recreating indexes. 
   * This may cause index naming conflicts if you have multiple, expression indexes that use the same column(s). 
   * Also, if your child table names are close to the object length limit (63 chars), you may run into naming conflicts when the index name truncates the original table name to add _idx or _pkey.
   * Please **DO NOT** use this tool to reindex your partition set if either of these cases apply! Use the --dryrun option first to check.
 * --parent (-p):          Parent table of an already created partition set. Required.
 * --connection (-c):      Connection string for use by psycopg to connect to your database. Defaults to "host=localhost". Highly recommended to use .pgpass file to keep credentials secure.
 * --concurrent:           Create indexes with the CONCURRENTLY option. Note this does not work on primary keys when --primary is given.
 * --primary:              By default the primary key is not recreated. Set this option if that is needed. Note this will cause an exclusive lock on the child table.
 * --jobs (-j):            Use the python multiprocessing library to recreate indexes in parallel. Note that this is per table, not per index. Be very careful setting this option if load is a concern on your systems.
 * --wait (-w):            Wait the given number of seconds after indexes have finished being created on a table before moving on to the next. When used with -j, this will set the pause between the batches of parallel jobs instead.
 * --dryrun:               Show what the script will do without actually running it against the database. Highly recommend reviewing this before running.
 * --quiet:                Turn off all output.
