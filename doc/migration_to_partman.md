Migrating An Existing Partition Set to PG Partition Manager
===========================================================

This document is an aid for migrating an existing partitioned table set to using pg_partman. Please note that at this time, this guide is only for non-native, trigger-baed partitioning. Documentation for native partitioning is in the works, but it will be mostly focused on PostgreSQL 11 since 10 was very limited in its partitioning support. For now, the easiest way to migrate to a natively partitioned table is to create a brand new table and copy/move the data. 

pg_partman does not support having child table names that do not match its naming convention. I've tried to implement that several times, but it's too difficult to support in a general manner and just ends up hindering development or breaking a feature. Your situation likely isn't exactly like the ones below, but this should at least provide guidance on what is required. 

As always, if you can first test this migration on a development system, it is highly recommended. The full data set is not needed to test this and just the schema with a smaller set of data in each child should be sufficient enough to make sure it works properly.

The following are the example tables I will be using:

                                      Table "tracking.hits"
     Column |            Type             | Modifiers | Storage | Stats target | Description 
    --------+-----------------------------+-----------+---------+--------------+-------------
     id     | integer                     | not null  | plain   |              | 
     start  | timestamp without time zone | not null  | plain   |              | 
    Child tables: tracking.hits20160103,
                  tracking.hits20160110,
                  tracking.hits20160117

    insert into tracking.hits20160103 values (1, generate_series('2016-01-03 01:00:00'::timestamptz, '2016-01-09 23:00:00'::timestamptz, '1 hour'::interval));
    insert into tracking.hits20160110 values (1, generate_series('2016-01-10 01:00:00'::timestamptz, '2016-01-16 23:00:00'::timestamptz, '1 hour'::interval));
    insert into tracking.hits20160117 values (1, generate_series('2016-01-17 01:00:00'::timestamptz, '2016-01-23 23:00:00'::timestamptz, '1 hour'::interval));

                                      Table "tracking.hits"
     Column |            Type             | Modifiers | Storage | Stats target | Description 
    --------+-----------------------------+-----------+---------+--------------+-------------
     id     | integer                     | not null  | plain   |              | 
     start  | timestamp without time zone | not null  | plain   |              | 
    Child tables: tracking.hits1000,
                  tracking.hits2000,
                  tracking.hits3000

    insert into tracking.hits1000 values (generate_series(1000,1999), now());
    insert into tracking.hits2000 values (generate_series(2000,2999), now());
    insert into tracking.hits3000 values (generate_series(3000,3999), now());

                                      Table "tracking.hits"
     Column |            Type             | Modifiers | Storage | Stats target | Description 
    --------+-----------------------------+-----------+---------+--------------+-------------
     id     | integer                     | not null  | plain   |              | 
     start  | timestamp without time zone | not null  | plain   |              | 
    Child tables: tracking.hits_aa,
                  tracking.hits_bb,
                  tracking.hits_cc

    Data depends on partitioning type. See below.

Step 1
------
Disable calls to the run_maintenance() 

If you have any partitions currently maintained by pg_partman, you may be calling this already for them. They should be fine for the period of time this conversion is being done. This is to avoid any issues with only a partial configuration existing during conversion. If you are using the background worker, commenting out the "pg_partman_bgw.dbname" parameter in postgresql.conf and then reloading (SELECT pg_reload_conf();) should be sufficient to stop it from running. If you're running pg_partman on several databases in the cluster and you don't want to stop them all, you can also just remove the one you're doing the migration on from that same parameter.


Step 2
------
Stop all writes to the partition set being migrated if possible. If you cannot do this for the period of time the conversion will take, all of these following steps must be done in a single transaction to avoid write errors due table names changing and old & new triggers existing at the same time.


Step 3
------
Rename the existing partitions to new naming convention. pg_partman uses a static pattern of suffixes for all partitions, both time & serial. All suffixes start with the string "_p". Even the custom time intervals use the same patterns. All of them are listed here for your reference.

    _pYYYY                - Yearly (and any custom time greater than this)
    _pYYYY"q"Q            - Quarterly (double quotes required to add another string value inside a date/time format string)
    _pYYYY_MM             - Monthly (and all custom time intervals between yearly and monthly)
    _pIYYY"w"IW           - Weekly (ISO Year and ISO Week)
    _pYYYY_MM_DD          - Daily (and all custom time intervals between monthly and daily)
    _pYYYY_MM_DD_HH24MI   - Hourly, Half-Hourly, Quarter-Hourly (and all custom time intervals between daily and hourly)
    _pYYYY_MM_DD_HH24MISS - Only used with custom time if interval is less than 1 minute (cannot be less than 1 second)
    _p#####               - Serial/ID partition has a suffix that is the value of the lowest possible entry in that table (Ex: _p10, _p20000, etc)

Step 3a
-------
For converting either time or serial based partition sets, if you have the lower boundary value as part of the partition name already, then it's simply a matter of doing a rename with some substring formatting since that's the pattern that pg_partman itself uses. Say your table was partitioned weekly and your original format just had the first day of the week (sunday) for the partition name (as in the example above). You can see below that we had 3 partitions with the old naming pattern of "YYYYMMDD". Looking at the list above, you can see the new weekly pattern that pg_partman uses. 

A note about quarterly partitioning... the to_timestamp() function does not recognize the "Q" format string like to_char() does. Why? You can look in the postgres source and see the reasoning, but it makes no sense to me. I handle this inside the show_partition_info() function if you need a way to do this.

So a query like the following which first extracts the original name then reformats the suffix would work. It doesn't actually do the renaming, it just generates all the ALTER TABLE statements for you for all the child tables in the set. If all of them don't quite have the same pattern for some reason, you can easily just re-run this, editing things as needed, and filter the resulting list of ALTER TABLE statements accordingly.

    select 'ALTER TABLE '||n.nspname||'.'||c.relname||' RENAME TO '||substring(c.relname from 1 for 4)||'_p'||to_char(to_timestamp(substring(c.relname from 5), 'YYYYMMDD'), 'IYYY')||'w'||to_char(to_timestamp(substring(c.relname from 5), 'YYYYMMDD'), 'IW')||';'
            from pg_inherits h
            join pg_class c on h.inhrelid = c.oid
            join pg_namespace n on c.relnamespace = n.oid
            where h.inhparent::regclass = 'tracking.hits'::regclass
            order by c.relname;

Which outputs:
 
    ALTER TABLE tracking.hits20160103 RENAME TO hits_p2015w53;
    ALTER TABLE tracking.hits20160110 RENAME TO hits_p2016w01;
    ALTER TABLE tracking.hits20160117 RENAME TO hits_p2016w02;

Running that should rename your tables to look like this now:

       tablename   | schemaname 
    ---------------+------------
     hits          | tracking
     hits_p2015w53 | tracking
     hits_p2016w01 | tracking
     hits_p2016w02 | tracking

If you're migrating a serial/id based partition set, and also have the naming convention with the lowest possible value, you'd do something very similar. Everything would be the same as the time-series one above except the renaming would be slightly different. Since the number value can vary, if you didn't have that as the final suffix of the partition name, that could make pattern matching for a rename more challanging. Using my second example table above, it would be something like this.

    select 'ALTER TABLE '||n.nspname||'.'||c.relname||' RENAME TO '||substring(c.relname from 1 for 4)||'_p'||substring(c.relname from 5)||';'
        from pg_inherits h
        join pg_class c on h.inhrelid = c.oid
        join pg_namespace n on c.relnamespace = n.oid
        where h.inhparent::regclass = 'tracking.hits'::regclass
        order by c.relname;

    ALTER TABLE tracking.hits1000 RENAME TO hits_p1000;
    ALTER TABLE tracking.hits2000 RENAME TO hits_p2000;
    ALTER TABLE tracking.hits3000 RENAME TO hits_p3000;

     tablename | schemaname 
    -----------+------------
     hits      | tracking
     hits1000  | tracking
     hits2000  | tracking
     hits3000  | tracking

Step 3b
-------
If your partitioned sets are named in a manner that relates differently to the data contained, or just doesn't relate at all, you'll instead have to do the renaming based off the lowest value in the control column instead. I'll be using the example above with the _aa, _bb, & _cc suffixes.

If this is partitioned by time, assume the following data exists in the child tables:

    insert into tracking.hits_aa values (1, generate_series('2016-01-03 01:00:00'::timestamptz, '2016-01-09 23:00:00'::timestamptz, '1 hour'::interval));
    insert into tracking.hits_bb values (2, generate_series('2016-01-10 01:00:00'::timestamptz, '2016-01-16 23:00:00'::timestamptz, '1 hour'::interval));
    insert into tracking.hits_cc values (3, generate_series('2016-01-17 01:00:00'::timestamptz, '2016-01-23 23:00:00'::timestamptz, '1 hour'::interval));

This next step takes advantage of anonymous code blocks. It's basically writing pl/pgsql function code without creating an actual function. Just run this block of code, adjusting values as needed, right inside a psql session.

    DO $rename$
    DECLARE 
        v_min_val           timestamp;
        v_row               record;
        v_sql               text;
    BEGIN

    -- Adjust your parent table name in the for loop query
    FOR v_row IN
        SELECT n.nspname AS child_schema, c.relname AS child_table
            FROM pg_inherits h
            JOIN pg_class c ON h.inhrelid = c.oid
            JOIN pg_namespace n ON c.relnamespace = n.oid
            WHERE h.inhparent::regclass = 'tracking.hits'::regclass
            ORDER BY c.relname
    LOOP
        -- Substitute your control column's name here in the min() function
        v_sql := format('SELECT min(start) FROM %I.%I', v_row.child_schema, v_row.child_table);
        EXECUTE v_sql INTO v_min_val;

        -- Adjust the date_trunc here to account for whatever your partitioning interval is. 
        v_min_val := date_trunc('week', v_min_val);

        -- Build the sql statement to rename the child table
        -- Use the appropriate date/time string from the list above for your interval
        v_sql := format('ALTER TABLE %I.%I RENAME TO %I' 
                , v_row.child_schema 
                , v_row.child_table
                , substring(v_row.child_table from 1 for 4)||'_p'||to_char(v_min_val, 'IYYY"w"IW'));

        -- I just have it outputing the ALTER statement for review. If you'd like this code to actually run it, uncomment the EXECUTE below.
        RAISE NOTICE '%', v_sql;
        -- EXECUTE v_sql;
    END LOOP;

    END
    $rename$;

This will output something like this:

    NOTICE:  ALTER TABLE tracking.hits_aa RENAME TO hits_p2015w53
    NOTICE:  ALTER TABLE tracking.hits_bb RENAME TO hits_p2016w01
    NOTICE:  ALTER TABLE tracking.hits_cc RENAME TO hits_p2016w02

I'd recommend running it at least once with the final EXECUTE commented out to review what it generates. If it looks good, you can uncomment the EXECUTE and rename your tables!

If you've got a serial/id partition set, calculating the proper suffix value can be done by taking advantage of modulus arithmetic. Assume the following values in the same example partition set used before:

    insert into tracking.hits_aa values (generate_series(1100,1294), now());
    insert into tracking.hits_bb values (generate_series(2400,2991), now());
    insert into tracking.hits_cc values (generate_series(3602,3843), now());

We'll be partitioning by 1000 again and you can see none of the minimum values are that even.

    DO $rename$
    DECLARE 
        v_min_val           bigint;
        v_row               record;
        v_sql               text;
    BEGIN

    -- Adjust your parent table name in the for loop query
    FOR v_row IN
        SELECT n.nspname AS child_schema, c.relname AS child_table
            FROM pg_inherits h
            JOIN pg_class c ON h.inhrelid = c.oid
            JOIN pg_namespace n ON c.relnamespace = n.oid
            WHERE h.inhparent::regclass = 'tracking.hits'::regclass
            ORDER BY c.relname
    LOOP
        -- Substitute your control column's name here in the min() function
        v_sql := format('SELECT min(id) FROM %I.%I', v_row.child_schema, v_row.child_table);
        EXECUTE v_sql INTO v_min_val;

        -- Adjust the numerical value after the % to account for whatever your partitioning interval is. 
        v_min_val := v_min_val - (v_min_val % 1000);

        -- Build the sql statement to rename the child table
        v_sql := format('ALTER TABLE %I.%I RENAME TO %I' 
                , v_row.child_schema 
                , v_row.child_table
                , substring(v_row.child_table from 1 for 4)||'_p'||v_min_val::text);

        -- I just have it outputing the ALTER statement for review. If you'd like this code to actually run it, uncomment the EXECUTE below.
        RAISE NOTICE '%', v_sql;
        -- EXECUTE v_sql;
    END LOOP;

    END
    $rename$;

You can see this makes nice even partition names:

    NOTICE:  ALTER TABLE tracking.hits_aa RENAME TO hits_p1000
    NOTICE:  ALTER TABLE tracking.hits_bb RENAME TO hits_p2000
    NOTICE:  ALTER TABLE tracking.hits_cc RENAME TO hits_p3000


Step 4
------
Actual setup and trigger swap

I mentioned at the beginning that if you had ongoing writes, pretty much everything from Step 2 and on had to be done in a single transaction. Even if you don't have to worry about writes, I'd highly recommend doing steps 4a and 4b in a single transaction just to avoid weird trigger conflicts.

Step 4a
-------
Drop your current partitioning trigger after starting a transaction

    BEGIN;
    DROP TRIGGER myoldtrigger ON tracking.hits;

Step 4b
-------
Setup pg_partman to manage your partition set.

    SELECT partman.create_parent('tracking.hits', 'start', 'partman', 'weekly');
    COMMIT;

This single function call will add your old partition set into pg_partman's configuration, create a new trigger and possibly create some new child tables as well. pg_partman always keeps a minumum number of future partitions premade (based on the *premake* value in the config table or as a parameter to the create_parent() function), so if you don't have those yet, this step will take care of that as well. Adjust the parameters as needed and see the documentation for addtional options that are available. This call matches the time partition used in the example so far.

    \d+ tracking.hits
                                      Table "tracking.hits"
     Column |            Type             | Modifiers | Storage | Stats target | Description 
    --------+-----------------------------+-----------+---------+--------------+-------------
     id     | integer                     | not null  | plain   |              | 
     start  | timestamp without time zone | not null  | plain   |              | 
    Triggers:
        hits_part_trig BEFORE INSERT ON tracking.hits FOR EACH ROW EXECUTE PROCEDURE tracking.hits_part_trig_func()
    Child tables: tracking.hits_p2015w53,
                  tracking.hits_p2016w01,
                  tracking.hits_p2016w02,
                  tracking.hits_p2016w03,
                  tracking.hits_p2016w04,
                  tracking.hits_p2016w05,
                  tracking.hits_p2016w06,
                  tracking.hits_p2016w07,
                  tracking.hits_p2016w08,
                  tracking.hits_p2016w09

Note that I ran this create_parent() function on Feb 6th, 2016. This is the 5th week of the year. By default, the premake value is 4, so it created 4 weeks in the future. And since this was the initial creation, it also creates 4 tables in the past. Some of those tables already existed and, since their naming pattern matched pg_partman's, it handled that just fine.

This final step is exactly the same no matter the partitioning type or interval, so once you reach here, run COMMIT and you're done!

Schedule the run_maintenance() function to run (either via cron or the BGW) and future partition maintenance will be handled for you. Review the pg_partman.md documentation for additional configuration options.

If you have any issues with this migration document, please create an issue on Github.
