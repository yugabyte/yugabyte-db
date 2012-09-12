PostgreSQL Partition Manager Extension (pg_partman)
--------------------------------------------------

Extension to help make managing time or serial id based table partitioning easier.

For this extension, the attributes of the child partitions are all obtained from the original parent. This includes defaults, indexes (primary keys, unique, etc) as well as permissions (permissions not yet inherited. coming soon). While you would not normally create indexes on the parent of a partition set, doing so makes it much easier to manage in this case. There will be no data in the parent table (if everything is working right), so they will not take up any space or have any impact on system performance. Using the parent table as a control to the details of the child tables gives a more central place to manage things that's a little more natural than a configuration table or using setup functions.

*create_parent(p_parent_table text, p_control text, p_type part.partition_type, p_interval text, p_premake int DEFAULT 3, p_debug boolean DEFAULT false)*
 * Main function to create a partition set with one parent table and inherited children. Parent table must already exist. Please apply all indexes, constraints and permissions to parent table so they will propagate to children.
 * First parameter (p_parent_table) is the existing parent table
 * Second paramter (p_control) is the column that the partitioning will be based on. Must be a time based column (integer support for ID partitioning coming soon).
 * Third column (p_type) is one of 4 values to set the partitioning type that will be used
 
 > **time-static** - Trigger function inserts only into specifically named partitions (handles data for current partition, 2 partitions ahead and 1 behind).  Cannot handle inserts to parent table outside the hard-coded time window. Function is kept up to date by run_maintenance() function. Ideal for high TPS tables that get inserts of new data only.  
 > **time-dynamic** - Trigger function can insert into any child partition based on the value of the control column. More flexible but not as efficient as time-static. Be aware that if the appropriate partition doesn't yet exist for the data inserted, the insert will fail.  
 > **id-static** - Same functionality as time-static but for a numeric range instead of time. When the id value has reached 50% of the max value for that partition, it will automatically create the next partition in sequence if it doesn't yet exist. Does NOT require run_maintenance() function to create new partitions.  
 > **id-dynamic** - Same functionality and limitations as time-dynamic but for a numeric range instead of time. Uses same 50% rule as id-static to create future partitions. Does NOT require run_maintenance() function to create new partitions.  

 * Fourth parameter (p_interval) is the time or numeric range interval for each partition. Supported values are:

 > **yearly** - One partition per year  
 > **monthly** - One partition per month  
 > **weekly** - One partition per week. Follows ISO week date format (http://en.wikipedia.org/wiki/ISO_week_date). Partitions are named as YYYYwWW (ex: 2012w36).  
 > **daily** - One partition per day  
 > **hourly** - One partition per hour  
 > **half-hour** - One partition per 30 minute interval on the half-hour (1200, 1230)  
 > **quarter-hour** - One partition per 15 minute interval on the quarter-hour (1200, 1215, 1230, 1245)  
 > **<integer>** - For ID based partitions, the integer value range of that ID that should be set per partition. This is the actual integer value, not text values like time-based partitioning.  

 * Fifth paramter (p_premake) is how many additional partitions to always stay ahead of the current partition. Default value is 3. This will keep at minimum 4 partitions made, including the current one. For example, if today was Sept 6, 2012, and premake was set to 4 for a daily partition, then partitions would be made for the 6th as well as the 7th, 8th, 9th and 10th. 
 * Sixth parameter (p_debug) is to turn on additional debugging information (not yet working).

*run_maintenance()*
 * Run this function as a scheduled job (cronjob, etc) to automatically keep time-based partitioning working.
 * Every run checks all tables listed in the **part_config** table with the types **time-static** and **time-dynamic** to make sure they're up to date.
 * Will automatically pre-create the new partition, keeping ahead by the number set as the **premake**. 
 * Will automatically update the function for **time-static** partitioning to keep the parent table pointing at the correct partitions. When using time-static, run this function more often than the partitioning interval to keep the function running its most efficient. For example, if using quarter-hour, run every 5 minutes; if using daily, run hourly, etc.


