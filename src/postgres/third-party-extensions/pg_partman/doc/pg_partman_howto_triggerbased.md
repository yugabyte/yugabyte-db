Example Guide On Setting Up Trigger-based Partitioning
========================================

This HowTo guide will show you some examples of how to set up both simple, single level partitioning as well as multi-level sub-partitioning. It will also show you how to partition data out of a table that has existing data (see **Sub-partition ID->ID->ID**) and undo the partitioning of an existing partition set. For more details on what each function does and the additional features in this extension, please see the **pg_partman.md** documentation file. The examples in this document assume you are running at least v3.0.1 of pg_partman. If you need a howto for a previous version, please see an older release available on github. 

Note that all examples here are for non-native, trigger-based partitioning. Documentation for native partitioning is in the works, but it will mostly be centered around PostgreSQL 11 since 10 was very limited in its partitioning support.

### Simple Time Based: 1 Partition Per Day
```
    keith@keith=# \d partman_test.time_taptest_table
                  Table "partman_test.time_taptest_table"
     Column |           Type           | Collation | Nullable | Default 
    --------+--------------------------+-----------+----------+---------
     col1   | integer                  |           | not null | 
     col2   | text                     |           |          | 
     col3   | timestamp with time zone |           | not null | now()
    Indexes:
        "time_taptest_table_pkey" PRIMARY KEY, btree (col1)

    keith@keith=# SELECT partman.create_parent('partman_test.time_taptest_table', 'col3', 'partman', 'daily');
     create_parent 
    ---------------
     t
    (1 row)

    keith@keith=# \d+ partman_test.time_taptest_table
                                      Table "partman_test.time_taptest_table"
     Column |           Type           | Collation | Nullable | Default | Storage  | Stats target | Description 
    --------+--------------------------+-----------+----------+---------+----------+--------------+-------------
     col1   | integer                  |           | not null |         | plain    |              | 
     col2   | text                     |           |          |         | extended |              | 
     col3   | timestamp with time zone |           | not null | now()   | plain    |              | 
    Indexes:
        "time_taptest_table_pkey" PRIMARY KEY, btree (col1)
    Triggers:
        time_taptest_table_part_trig BEFORE INSERT ON partman_test.time_taptest_table FOR EACH ROW EXECUTE PROCEDURE partman_test.time_taptest_table_part_trig_func()
    Child tables: partman_test.time_taptest_table_p2017_03_23,
                  partman_test.time_taptest_table_p2017_03_24,
                  partman_test.time_taptest_table_p2017_03_25,
                  partman_test.time_taptest_table_p2017_03_26,
                  partman_test.time_taptest_table_p2017_03_27,
                  partman_test.time_taptest_table_p2017_03_28,
                  partman_test.time_taptest_table_p2017_03_29,
                  partman_test.time_taptest_table_p2017_03_30,
                  partman_test.time_taptest_table_p2017_03_31
```
The trigger function most efficiently covers a specific period of time for 4 days before and 4 days after today. This can be adjusted with the `optimize_trigger` config option in the `part_config` table. Outside of that, a dynamic statement tries to find the appropriate child table to put the data into. Note this dynamic statement is far less efficient since a catalog lookup is required and the statement plan cannot be cached as well as looking up the that the child table exists. If the child table does not exist at all for the time value given, the data goes to the parent:
```
keith@keith=# \sf partman_test.time_taptest_table_part_trig_func 
CREATE OR REPLACE FUNCTION partman_test.time_taptest_table_part_trig_func()
 RETURNS trigger
 LANGUAGE plpgsql
AS $function$
            DECLARE
            v_count                 int;
            v_partition_name        text;
            v_partition_timestamp   timestamptz;
        BEGIN 
        IF TG_OP = 'INSERT' THEN 
            v_partition_timestamp := date_trunc('day', NEW.col3);
            IF NEW.col3 >= '2017-03-27 00:00:00-04' AND NEW.col3 < '2017-03-28 00:00:00-04' THEN 
            INSERT INTO partman_test.time_taptest_table_p2017_03_27 VALUES (NEW.*) ; 
            ELSIF NEW.col3 >= '2017-03-26 00:00:00-04' AND NEW.col3 < '2017-03-27 00:00:00-04' THEN 
                INSERT INTO partman_test.time_taptest_table_p2017_03_26 VALUES (NEW.*) ; 
            ELSIF NEW.col3 >= '2017-03-28 00:00:00-04' AND NEW.col3 < '2017-03-29 00:00:00-04' THEN 
                INSERT INTO partman_test.time_taptest_table_p2017_03_28 VALUES (NEW.*) ;
            ELSIF NEW.col3 >= '2017-03-25 00:00:00-04' AND NEW.col3 < '2017-03-26 00:00:00-04' THEN 
                INSERT INTO partman_test.time_taptest_table_p2017_03_25 VALUES (NEW.*) ; 
            ELSIF NEW.col3 >= '2017-03-29 00:00:00-04' AND NEW.col3 < '2017-03-30 00:00:00-04' THEN 
                INSERT INTO partman_test.time_taptest_table_p2017_03_29 VALUES (NEW.*) ;
            ELSIF NEW.col3 >= '2017-03-24 00:00:00-04' AND NEW.col3 < '2017-03-25 00:00:00-04' THEN 
                INSERT INTO partman_test.time_taptest_table_p2017_03_24 VALUES (NEW.*) ; 
            ELSIF NEW.col3 >= '2017-03-30 00:00:00-04' AND NEW.col3 < '2017-03-31 00:00:00-04' THEN 
                INSERT INTO partman_test.time_taptest_table_p2017_03_30 VALUES (NEW.*) ;
            ELSIF NEW.col3 >= '2017-03-23 00:00:00-04' AND NEW.col3 < '2017-03-24 00:00:00-04' THEN 
                INSERT INTO partman_test.time_taptest_table_p2017_03_23 VALUES (NEW.*) ; 
            ELSIF NEW.col3 >= '2017-03-31 00:00:00-04' AND NEW.col3 < '2017-04-01 00:00:00-04' THEN 
                INSERT INTO partman_test.time_taptest_table_p2017_03_31 VALUES (NEW.*) ;
            ELSE
                v_partition_name := partman.check_name_length('time_taptest_table', to_char(v_partition_timestamp, 'YYYY_MM_DD'), TRUE);
                SELECT count(*) INTO v_count FROM pg_catalog.pg_tables WHERE schemaname = 'partman_test'::name AND tablename = v_partition_name::name;
                IF v_count > 0 THEN 
                    EXECUTE format('INSERT INTO %I.%I VALUES($1.*) ', 'partman_test', v_partition_name) USING NEW;
                ELSE
                    RETURN NEW;
                END IF;
            END IF;
        END IF;
        RETURN NULL;
        END $function$
```
### Simple Serial ID: 1 Partition Per 10 ID Values Starting With Empty Table
```
    keith=# \d partman_test.id_taptest_table
                   Table "partman_test.id_taptest_table"
     Column |           Type           |           Modifiers            
    --------+--------------------------+--------------------------------
     col1   | integer                  | not null
     col2   | text                     | not null default 'stuff'::text
     col3   | timestamp with time zone | default now()
    Indexes:
        "id_taptest_table_pkey" PRIMARY KEY, btree (col1)


    keith=# SELECT create_parent('partman_test.id_taptest_table', 'col1', 'partman', '10');
     create_parent 
    ---------------
     t
    (1 row)


    keith=# \d+ partman_test.id_taptest_table
                                       Table "partman_test.id_taptest_table"
     Column |           Type           |           Modifiers            | Storage  | Stats target | Description 
    --------+--------------------------+--------------------------------+----------+--------------+-------------
     col1   | integer                  | not null                       | plain    |              | 
     col2   | text                     | not null default 'stuff'::text | extended |              | 
     col3   | timestamp with time zone | default now()                  | plain    |              | 
    Indexes:
        "id_taptest_table_pkey" PRIMARY KEY, btree (col1)
    Triggers:
        id_taptest_table_part_trig BEFORE INSERT ON partman_test.id_taptest_table FOR EACH ROW EXECUTE PROCEDURE partman_test.id_taptest_table_part_trig_func()
    Child tables: partman_test.id_taptest_table_p0,
                  partman_test.id_taptest_table_p10,
                  partman_test.id_taptest_table_p20,
                  partman_test.id_taptest_table_p30,
                  partman_test.id_taptest_table_p40
```
This trigger function most efficiently covers for 4x10 intervals above the current max (0). Once max id reaches higher values, it will also efficiently cover up to 4x10 intervals behind the current max.
Outside of that, a dynamic statement tries to find the appropriate child table to put the data into. And like I said for time above, the dynamic part is less efficient.
```
    keith@keith=# \sf partman_test.id_taptest_table_part_trig_func 
    CREATE OR REPLACE FUNCTION partman_test.id_taptest_table_part_trig_func()
     RETURNS trigger
     LANGUAGE plpgsql
    AS $function$ 
        DECLARE
            v_count                     int;
            v_current_partition_id      bigint;
            v_current_partition_name    text;
            v_id_position               int;
            v_last_partition            text := 'id_taptest_table_p40';
            v_next_partition_id         bigint;
            v_next_partition_name       text;
            v_partition_created         boolean;
        BEGIN
        IF TG_OP = 'INSERT' THEN 
            IF NEW.col1 >= 0 AND NEW.col1 < 10 THEN  
                INSERT INTO partman_test.id_taptest_table_p0 VALUES (NEW.*) ; 
            ELSIF NEW.col1 >= 10 AND NEW.col1 < 20 THEN 
                INSERT INTO partman_test.id_taptest_table_p10 VALUES (NEW.*) ;
            ELSIF NEW.col1 >= 20 AND NEW.col1 < 30 THEN 
                INSERT INTO partman_test.id_taptest_table_p20 VALUES (NEW.*) ;
            ELSIF NEW.col1 >= 30 AND NEW.col1 < 40 THEN 
                INSERT INTO partman_test.id_taptest_table_p30 VALUES (NEW.*) ;
            ELSIF NEW.col1 >= 40 AND NEW.col1 < 50 THEN 
                INSERT INTO partman_test.id_taptest_table_p40 VALUES (NEW.*) ;
            ELSE
                v_current_partition_id := NEW.col1 - (NEW.col1 % 10);
                v_current_partition_name := partman.check_name_length('id_taptest_table', v_current_partition_id::text, TRUE);
                SELECT count(*) INTO v_count FROM pg_catalog.pg_tables WHERE schemaname = 'partman_test'::name AND tablename = v_current_partition_name::n
    ame;
                IF v_count > 0 THEN 
                    EXECUTE format('INSERT INTO %I.%I VALUES($1.*) ', 'partman_test', v_current_partition_name) USING NEW;
                ELSE
                    RETURN NEW;
                END IF;
            END IF;
        END IF;
        RETURN NULL;
        END $function$
```

### Simple Serial ID: 1 Partition Per 10 ID Values Starting With Empty Table and using upsert to drop conflicting rows

```
    Uses same example table as above

    keith@keith=# SELECT partman.create_parent('partman_test.id_taptest_table', 'col1', 'partman', '10', p_upsert := 'ON CONFLICT (col1) DO NOTHING');
     create_parent 
    ---------------
     t
    (1 row)

    keith@keith=# \d+ partman_test.id_taptest_table
                                          Table "partman_test.id_taptest_table"
     Column |           Type           | Collation | Nullable |    Default    | Storage  | Stats target | Description 
    --------+--------------------------+-----------+----------+---------------+----------+--------------+-------------
     col1   | integer                  |           | not null |               | plain    |              | 
     col2   | text                     |           | not null | 'stuff'::text | extended |              | 
     col3   | timestamp with time zone |           |          | now()         | plain    |              | 
    Indexes:
        "id_taptest_table_pkey" PRIMARY KEY, btree (col1)
    Triggers:
        id_taptest_table_part_trig BEFORE INSERT ON partman_test.id_taptest_table FOR EACH ROW EXECUTE PROCEDURE partman_test.id_taptest_table_part_trig_func()
    Child tables: partman_test.id_taptest_table_p0,
                  partman_test.id_taptest_table_p10,
                  partman_test.id_taptest_table_p20,
                  partman_test.id_taptest_table_p30,
                  partman_test.id_taptest_table_p40
```
Other than the new ON CONFLICT clause, this trigger function works exactly the same as the previous ID example.
```
    keith@keith=# \sf partman_test.id_taptest_table_part_trig_func 
    CREATE OR REPLACE FUNCTION partman_test.id_taptest_table_part_trig_func()
     RETURNS trigger
     LANGUAGE plpgsql
    AS $function$ 
        DECLARE
            v_count                     int;
            v_current_partition_id      bigint;
            v_current_partition_name    text;
            v_id_position               int;
            v_last_partition            text := 'id_taptest_table_p40';
            v_next_partition_id         bigint;
            v_next_partition_name       text;
            v_partition_created         boolean;
        BEGIN
        IF TG_OP = 'INSERT' THEN 
            IF NEW.col1 >= 0 AND NEW.col1 < 10 THEN  
                INSERT INTO partman_test.id_taptest_table_p0 VALUES (NEW.*) ON CONFLICT (col1) DO NOTHING; 
            ELSIF NEW.col1 >= 10 AND NEW.col1 < 20 THEN 
                INSERT INTO partman_test.id_taptest_table_p10 VALUES (NEW.*) ON CONFLICT (col1) DO NOTHING;
            ELSIF NEW.col1 >= 20 AND NEW.col1 < 30 THEN 
                INSERT INTO partman_test.id_taptest_table_p20 VALUES (NEW.*) ON CONFLICT (col1) DO NOTHING;
            ELSIF NEW.col1 >= 30 AND NEW.col1 < 40 THEN 
                INSERT INTO partman_test.id_taptest_table_p30 VALUES (NEW.*) ON CONFLICT (col1) DO NOTHING;
            ELSIF NEW.col1 >= 40 AND NEW.col1 < 50 THEN 
                INSERT INTO partman_test.id_taptest_table_p40 VALUES (NEW.*) ON CONFLICT (col1) DO NOTHING;
            ELSE
                v_current_partition_id := NEW.col1 - (NEW.col1 % 10);
                v_current_partition_name := partman.check_name_length('id_taptest_table', v_current_partition_id::text, TRUE);
                SELECT count(*) INTO v_count FROM pg_catalog.pg_tables WHERE schemaname = 'partman_test'::name AND tablename = v_current_partition_name::n
    ame;
                IF v_count > 0 THEN 
                    EXECUTE format('INSERT INTO %I.%I VALUES($1.*) ON CONFLICT (col1) DO NOTHING', 'partman_test', v_current_partition_name) USING NEW;
                ELSE
                    RETURN NEW;
                END IF;
            END IF;
        END IF;
        RETURN NULL;
        END $function$
```
Running the following query will insert a row in the table
```
    keith@keith=# INSERT INTO partman_test.id_taptest_table(col1,col2) VALUES(1,'insert1');
    INSERT 0 0
    Time: 4.876 ms
    keith@keith=# SELECT * FROM partman_test.id_taptest_table;
     col1 |  col2   |             col3              
    ------+---------+-------------------------------
        1 | insert1 | 2017-03-27 14:23:02.769999-04
    (1 row)
```
Running the following query will not fail but the row in the table will not change and col2 will still be 'insert1'
```
    keith@keith=# INSERT INTO partman_test.id_taptest_table(col1,col2) VALUES(1,'insert2');
    INSERT 0 0
    Time: 1.583 ms
    keith@keith=# SELECT * FROM partman_test.id_taptest_table;
     col1 |  col2   |             col3              
    ------+---------+-------------------------------
        1 | insert1 | 2017-03-27 14:23:02.769999-04
    (1 row)
```


### Simple Serial ID: 1 Partition Per 10 ID Values Starting With Empty Table and using upsert to update conflicting rows
```
    Uses same example table as above

    keith@keith=# SELECT partman.create_parent('partman_test.id_taptest_table', 'col1', 'partman', '10', p_upsert := 'ON CONFLICT (col1) DO UPDATE SET col2=EXCLUDED.col2, col3=EXCLUDED.col3');
     create_parent 
    ---------------
     t
    (1 row)

    keith@keith=# \d+ partman_test.id_taptest_table
                                          Table "partman_test.id_taptest_table"
     Column |           Type           | Collation | Nullable |    Default    | Storage  | Stats target | Description 
    --------+--------------------------+-----------+----------+---------------+----------+--------------+-------------
     col1   | integer                  |           | not null |               | plain    |              | 
     col2   | text                     |           | not null | 'stuff'::text | extended |              | 
     col3   | timestamp with time zone |           |          | now()         | plain    |              | 
    Indexes:
        "id_taptest_table_pkey" PRIMARY KEY, btree (col1)
    Triggers:
        id_taptest_table_part_trig BEFORE INSERT ON partman_test.id_taptest_table FOR EACH ROW EXECUTE PROCEDURE partman_test.id_taptest_table_part_trig_func()
    Child tables: partman_test.id_taptest_table_p0,
                  partman_test.id_taptest_table_p10,
                  partman_test.id_taptest_table_p20,
                  partman_test.id_taptest_table_p30,
                  partman_test.id_taptest_table_p40
```
Other than the new ON CONFLICT clause, this trigger function works exactly the same as the previous ID example.
```
    keith@keith=# \sf partman_test.id_taptest_table_part_trig_func 
    CREATE OR REPLACE FUNCTION partman_test.id_taptest_table_part_trig_func()
     RETURNS trigger
     LANGUAGE plpgsql
    AS $function$ 
        DECLARE
            v_count                     int;
            v_current_partition_id      bigint;
            v_current_partition_name    text;
            v_id_position               int;
            v_last_partition            text := 'id_taptest_table_p40';
            v_next_partition_id         bigint;
            v_next_partition_name       text;
            v_partition_created         boolean;
        BEGIN
        IF TG_OP = 'INSERT' THEN 
            IF NEW.col1 >= 0 AND NEW.col1 < 10 THEN  
                INSERT INTO partman_test.id_taptest_table_p0 VALUES (NEW.*) ON CONFLICT (col1) DO UPDATE SET col2=EXCLUDED.col2, col3=EXCLUDED.col3; 
            ELSIF NEW.col1 >= 10 AND NEW.col1 < 20 THEN 
                INSERT INTO partman_test.id_taptest_table_p10 VALUES (NEW.*) ON CONFLICT (col1) DO UPDATE SET col2=EXCLUDED.col2, col3=EXCLUDED.col3;
            ELSIF NEW.col1 >= 20 AND NEW.col1 < 30 THEN 
                INSERT INTO partman_test.id_taptest_table_p20 VALUES (NEW.*) ON CONFLICT (col1) DO UPDATE SET col2=EXCLUDED.col2, col3=EXCLUDED.col3;
            ELSIF NEW.col1 >= 30 AND NEW.col1 < 40 THEN 
                INSERT INTO partman_test.id_taptest_table_p30 VALUES (NEW.*) ON CONFLICT (col1) DO UPDATE SET col2=EXCLUDED.col2, col3=EXCLUDED.col3;
            ELSIF NEW.col1 >= 40 AND NEW.col1 < 50 THEN 
                INSERT INTO partman_test.id_taptest_table_p40 VALUES (NEW.*) ON CONFLICT (col1) DO UPDATE SET col2=EXCLUDED.col2, col3=EXCLUDED.col3;
            ELSE
                v_current_partition_id := NEW.col1 - (NEW.col1 % 10);
                v_current_partition_name := partman.check_name_length('id_taptest_table', v_current_partition_id::text, TRUE);
                SELECT count(*) INTO v_count FROM pg_catalog.pg_tables WHERE schemaname = 'partman_test'::name AND tablename = v_current_partition_name::n
    ame;
                IF v_count > 0 THEN 
                    EXECUTE format('INSERT INTO %I.%I VALUES($1.*) ON CONFLICT (col1) DO UPDATE SET col2=EXCLUDED.col2, col3=EXCLUDED.col3', 'partman_test
    ', v_current_partition_name) USING NEW;
                ELSE
                    RETURN NEW;
                END IF;
            END IF;
        END IF;
        RETURN NULL;
        END $function$
```
Running the following query will insert a row in the table
```
    keith@keith=# INSERT INTO partman_test.id_taptest_table(col1,col2) VALUES(1,'insert1');
    INSERT 0 0
    Time: 6.012 ms
    keith@keith=# SELECT * FROM partman_test.id_taptest_table;
     col1 |  col2   |             col3             
    ------+---------+------------------------------
        1 | insert1 | 2017-03-27 14:32:26.59552-04
    (1 row)
```
Running the following query will not fail and the row in the table will change and col2 will now be 'insert2' and the timestamp in col3 will update to the default value now()
```
    keith@keith=# INSERT INTO partman_test.id_taptest_table(col1,col2) VALUES(1,'insert2');
    INSERT 0 0
    Time: 4.235 ms
    keith@keith=# SELECT * FROM partman_test.id_taptest_table;
     col1 |  col2   |             col3              
    ------+---------+-------------------------------
        1 | insert2 | 2017-03-27 14:33:00.949928-04
    (1 row)
```

### Sub-partition Time->Time->Time: Yearly -> Monthly -> Daily
```
    keith@keith=# \d partman_test.time_taptest_table
                  Table "partman_test.time_taptest_table"
     Column |           Type           | Collation | Nullable | Default 
    --------+--------------------------+-----------+----------+---------
     col1   | integer                  |           | not null | 
     col2   | text                     |           |          | 
     col3   | timestamp with time zone |           | not null | now()
    Indexes:
        "time_taptest_table_pkey" PRIMARY KEY, btree (col1)
```
Create top yearly partition set that only covers 2 years forward/back
```
    keith@keith=# SELECT partman.create_parent('partman_test.time_taptest_table', 'col3', 'partman', 'yearly', p_premake := 2);
     create_parent 
    ---------------
     t
    (1 row)
```
Now tell pg_partman to partition all yearly child tables by month. Do this by giving it the parent table of the yearly partition set (happens to be the same as above)
```
    keith@keith=# SELECT partman.create_sub_parent('partman_test.time_taptest_table', 'col3', 'partman', 'monthly', p_premake := 2);
     create_sub_parent 
    -------------------
     t
    (1 row)

    keith@keith=# SELECT tablename FROM pg_tables WHERE schemaname = 'partman_test' ORDER BY tablename;
                 tablename             
    -----------------------------------
     time_taptest_table
     time_taptest_table_p2015
     time_taptest_table_p2015_p2015_01
     time_taptest_table_p2016
     time_taptest_table_p2016_p2016_01
     time_taptest_table_p2017
     time_taptest_table_p2017_p2017_01
     time_taptest_table_p2017_p2017_02
     time_taptest_table_p2017_p2017_03
     time_taptest_table_p2017_p2017_04
     time_taptest_table_p2017_p2017_05
     time_taptest_table_p2018
     time_taptest_table_p2018_p2018_01
     time_taptest_table_p2019
     time_taptest_table_p2019_p2019_01
    (15 rows)
```
The day this tutorial was updated is 2017-03-27. You now see that this causes only 2 new future partitions to be created. And for the monthly partitions, they have been created to cover 2 months ahead as well. Note that the trigger will still cover 4 ahead and 4 behind for both partition levels unless you change the `optimize_trigger` option in the config table. A parent table ALWAYS has at least one child, so for the time period that is outside of what the premake covers, just a single table has been made for the lowest possible month in that yearly time period (January). Now tell pg_partman to partition every monthly table that currently exists by day. Do this by giving it the parent table of each monthly partition set (the parent with the just the year suffix since its children are the monthly partitions).
```
    SELECT partman.create_sub_parent('partman_test.time_taptest_table_p2015', 'col3', 'partman', 'daily', p_premake := 2);
    SELECT partman.create_sub_parent('partman_test.time_taptest_table_p2016', 'col3', 'partman', 'daily', p_premake := 2);
    SELECT partman.create_sub_parent('partman_test.time_taptest_table_p2017', 'col3', 'partman', 'daily', p_premake := 2);
    SELECT partman.create_sub_parent('partman_test.time_taptest_table_p2018', 'col3', 'partman', 'daily', p_premake := 2);
    SELECT partman.create_sub_parent('partman_test.time_taptest_table_p2019', 'col3', 'partman', 'daily', p_premake := 2);

    keith@keith=# SELECT tablename FROM pg_tables WHERE schemaname = 'partman_test' ORDER BY tablename;
                       tablename                   
    -----------------------------------------------
     time_taptest_table
     time_taptest_table_p2015
     time_taptest_table_p2015_p2015_01
     time_taptest_table_p2015_p2015_01_p2015_01_01
     time_taptest_table_p2016
     time_taptest_table_p2016_p2016_01
     time_taptest_table_p2016_p2016_01_p2016_01_01
     time_taptest_table_p2017
     time_taptest_table_p2017_p2017_01
     time_taptest_table_p2017_p2017_01_p2017_01_01
     time_taptest_table_p2017_p2017_02
     time_taptest_table_p2017_p2017_02_p2017_02_01
     time_taptest_table_p2017_p2017_03
     time_taptest_table_p2017_p2017_03_p2017_03_25
     time_taptest_table_p2017_p2017_03_p2017_03_26
     time_taptest_table_p2017_p2017_03_p2017_03_27
     time_taptest_table_p2017_p2017_03_p2017_03_28
     time_taptest_table_p2017_p2017_03_p2017_03_29
     time_taptest_table_p2017_p2017_04
     time_taptest_table_p2017_p2017_04_p2017_04_01
     time_taptest_table_p2017_p2017_05
     time_taptest_table_p2017_p2017_05_p2017_05_01
     time_taptest_table_p2018
     time_taptest_table_p2018_p2018_01
     time_taptest_table_p2018_p2018_01_p2018_01_01
     time_taptest_table_p2019
     time_taptest_table_p2019_p2019_01
     time_taptest_table_p2019_p2019_01_p2019_01_01
    (28 rows)
```
Again, assuming today's date is 2017-03-27, it has created the sub-partitions to cover 2 days in the future. All other parent tables outside of the current time period have the lowest possible day created for them.


### Sub-partition ID->ID->ID: 10,000 -> 1,000 -> 100

This partition set has existing data already in it. We will partition it out using the python script found in the "bin" directory of the repo. It is possible to use the partition_data_id() function in postgres as well, but that would partition all the data out in a single transaction which, for a live table, could cause serious contention & I/O issues. The python script allows commits to be done in batches and avoid that contention and you can add a pause in between batches to limit I/O activity. The p_jobmon flag being set in the creation functions is done just to keep the spamming of the jobmon logs to a minimum for these test examples.
```
    keith@keith=# \d partman_test.id_taptest_table
                      Table "partman_test.id_taptest_table"
     Column |           Type           | Collation | Nullable |    Default    
    --------+--------------------------+-----------+----------+---------------
     col1   | integer                  |           | not null | 
     col2   | text                     |           | not null | 'stuff'::text
     col3   | timestamp with time zone |           |          | now()
    Indexes:
        "id_taptest_table_pkey" PRIMARY KEY, btree (col1)

    
    keith@keith=# SELECT count(*) FROM partman_test.id_taptest_table ;
     count  
    --------
     100000
    (1 row)

    keith@keith=# SELECT min(col1), max(col1) FROM partman_test.id_taptest_table ;
     min |  max   
    -----+--------
       1 | 100000
    (1 row)

```
Since there is already data in the table, the child tables initially created will be based around the max value, two before it and two after it. As stated above for time, the trigger still covers for 4 partitions before & after most efficiently, so if you need to adjust that as well, see the `part_config` table.
```
    keith@keith=# SELECT partman.create_parent('partman_test.id_taptest_table', 'col1', 'partman', '10000', p_jobmon := false, p_premake := 2); 
    ---------------
     t
    (1 row)

    keith@keith=# SELECT tablename FROM pg_tables WHERE schemaname = 'partman_test' ORDER BY tablename;
            tablename         
    --------------------------
     id_taptest_table
     id_taptest_table_p100000
     id_taptest_table_p110000
     id_taptest_table_p120000
     id_taptest_table_p80000
     id_taptest_table_p90000
    (6 rows)
```
However, the data still resides in the parent table at this time. To partition it out, use the python script as mentioned above. The options below will cause it to commit every 100 rows. If the interval option was not given, it would commit them at the configured interval of 10,000. Allowing a lower interval decreases the possible contention and allows the data to be more readily available in the newly created partitions:
```
    $ python partition_data.py -c host=localhost -p partman_test.id_taptest_table -t id -i 100
    Attempting to turn off autovacuum for partition set...
    ... Success!
    Rows moved: 100
    Rows moved: 100
    ...
    Rows moved: 99
    ...
    Rows moved: 100
    Rows moved: 1
    Total rows moved: 100000
    Running vacuum analyze on parent table...
    Attempting to reset autovacuum for old parent table and all child tables...
        ... Success!
```
Partitioning the data like this has also made the partitions that were needed to store the data
```
    keith@keith=# SELECT tablename FROM pg_tables WHERE schemaname = 'partman_test' ORDER BY tablename;
            tablename         
    --------------------------
     id_taptest_table
     id_taptest_table_p0
     id_taptest_table_p10000
     id_taptest_table_p100000
     id_taptest_table_p110000
     id_taptest_table_p120000
     id_taptest_table_p20000
     id_taptest_table_p30000
     id_taptest_table_p40000
     id_taptest_table_p50000
     id_taptest_table_p60000
     id_taptest_table_p70000
     id_taptest_table_p80000
     id_taptest_table_p90000
    (14 rows)
```
Now create the sub-partitions for 1000. As was noted above for time, we give the parent table who's children we want partitioned along with the properties to give those children:
```
    keith@keith=# SELECT partman.create_sub_parent('partman_test.id_taptest_table', 'col1', 'partman', '1000', p_jobmon := false, p_premake := 2);
     create_sub_parent 
    -------------------
     t
    (1 row)
```
All children tables get at least their minimum sub-partition made and the sub-partitions based around the current max value are also created. 
```
    keith@keith=# SELECT tablename FROM pg_tables WHERE schemaname = 'partman_test' ORDER BY tablename;
                tablename             
    ----------------------------------
     id_taptest_table
     id_taptest_table_p0
     id_taptest_table_p0_p0
     id_taptest_table_p10000
     id_taptest_table_p100000
     id_taptest_table_p100000_p100000
     id_taptest_table_p100000_p101000
     id_taptest_table_p100000_p102000
     id_taptest_table_p10000_p10000
     id_taptest_table_p110000
     id_taptest_table_p110000_p110000
     id_taptest_table_p120000
     id_taptest_table_p120000_p120000
     id_taptest_table_p20000
     id_taptest_table_p20000_p20000
     id_taptest_table_p30000
     id_taptest_table_p30000_p30000
     id_taptest_table_p40000
     id_taptest_table_p40000_p40000
     id_taptest_table_p50000
     id_taptest_table_p50000_p50000
     id_taptest_table_p60000
     id_taptest_table_p60000_p60000
     id_taptest_table_p70000
     id_taptest_table_p70000_p70000
     id_taptest_table_p80000
     id_taptest_table_p80000_p80000
     id_taptest_table_p90000
     id_taptest_table_p90000_p98000
     id_taptest_table_p90000_p99000
    (30 rows)
```
If you're wondering why, even with data in them, the children didn't get all their sub-partitions created, it's for the same reason that the top partition only initially had the 2 previous and 2 after created: the data still exists in the sub-partition parents. You can see this by running the monitoring function built into pg_partman here:
```
    keith@keith=# SELECT * FROM partman.check_parent() ORDER BY 1;
                 parent_table              | count 
    ---------------------------------------+-------
     partman_test.id_taptest_table_p0      |  9999
     partman_test.id_taptest_table_p10000  | 10000
     partman_test.id_taptest_table_p100000 |     1
     partman_test.id_taptest_table_p20000  | 10000
     partman_test.id_taptest_table_p30000  | 10000
     partman_test.id_taptest_table_p40000  | 10000
     partman_test.id_taptest_table_p50000  | 10000
     partman_test.id_taptest_table_p60000  | 10000
     partman_test.id_taptest_table_p70000  | 10000
     partman_test.id_taptest_table_p80000  | 10000
     partman_test.id_taptest_table_p90000  | 10000
    (11 rows)
```
So, lets fix that:
```
    python partition_data.py -c host=localhost -p partman_test.id_taptest_table_p0 -t id -i 100
    python partition_data.py -c host=localhost -p partman_test.id_taptest_table_p10000 -t id -i 100
    python partition_data.py -c host=localhost -p partman_test.id_taptest_table_p20000 -t id -i 100
    python partition_data.py -c host=localhost -p partman_test.id_taptest_table_p30000 -t id -i 100
    python partition_data.py -c host=localhost -p partman_test.id_taptest_table_p40000 -t id -i 100
    python partition_data.py -c host=localhost -p partman_test.id_taptest_table_p50000 -t id -i 100
    python partition_data.py -c host=localhost -p partman_test.id_taptest_table_p60000 -t id -i 100
    python partition_data.py -c host=localhost -p partman_test.id_taptest_table_p70000 -t id -i 100
    python partition_data.py -c host=localhost -p partman_test.id_taptest_table_p80000 -t id -i 100
    python partition_data.py -c host=localhost -p partman_test.id_taptest_table_p90000 -t id -i 100
    python partition_data.py -c host=localhost -p partman_test.id_taptest_table_p100000 -t id -i 100
```
Now the monitoring function returns nothing (as should be the norm):
```
    keith@keith=# SELECT * FROM partman.check_parent() ORDER BY 1;
     parent_table | count 
    --------------+-------
    (0 rows)
```
Now we also see all child partitions were created for the data that exists:
```
    keith@keith=# SELECT tablename FROM pg_tables WHERE schemaname = 'partman_test' ORDER BY tablename;
                tablename             
    ----------------------------------
     id_taptest_table
     id_taptest_table_p0
     id_taptest_table_p0_p0
     id_taptest_table_p0_p1000
     id_taptest_table_p0_p2000
     id_taptest_table_p0_p3000
     id_taptest_table_p0_p4000
     id_taptest_table_p0_p5000
     id_taptest_table_p0_p6000
     id_taptest_table_p0_p7000
     id_taptest_table_p0_p8000
     id_taptest_table_p0_p9000
     id_taptest_table_p10000
     id_taptest_table_p100000
     id_taptest_table_p100000_p100000
     id_taptest_table_p100000_p101000
     id_taptest_table_p100000_p102000
     id_taptest_table_p10000_p10000
     id_taptest_table_p10000_p11000
     id_taptest_table_p10000_p12000
     id_taptest_table_p10000_p13000
     id_taptest_table_p10000_p14000
     id_taptest_table_p10000_p15000
     id_taptest_table_p10000_p16000
     id_taptest_table_p10000_p17000
     id_taptest_table_p10000_p18000
     id_taptest_table_p10000_p19000
     id_taptest_table_p110000
     id_taptest_table_p110000_p110000
     id_taptest_table_p120000
     id_taptest_table_p120000_p120000
     id_taptest_table_p20000
     id_taptest_table_p20000_p20000
     id_taptest_table_p20000_p21000
     id_taptest_table_p20000_p22000
     id_taptest_table_p20000_p23000
     id_taptest_table_p20000_p24000
     id_taptest_table_p20000_p25000
     id_taptest_table_p20000_p26000
     id_taptest_table_p20000_p27000
     id_taptest_table_p20000_p28000
     id_taptest_table_p20000_p29000
     id_taptest_table_p30000
     id_taptest_table_p30000_p30000
     id_taptest_table_p30000_p31000
     id_taptest_table_p30000_p32000
     id_taptest_table_p30000_p33000
     id_taptest_table_p30000_p34000
     id_taptest_table_p30000_p35000
     id_taptest_table_p30000_p36000
     id_taptest_table_p30000_p37000
     id_taptest_table_p30000_p38000
     id_taptest_table_p30000_p39000
     id_taptest_table_p40000
     id_taptest_table_p40000_p40000
     id_taptest_table_p40000_p41000
     id_taptest_table_p40000_p42000
     id_taptest_table_p40000_p43000
     id_taptest_table_p40000_p44000
     id_taptest_table_p40000_p45000
     id_taptest_table_p40000_p46000
     id_taptest_table_p40000_p47000
     id_taptest_table_p40000_p48000
     id_taptest_table_p40000_p49000
     id_taptest_table_p50000
     id_taptest_table_p50000_p50000
     id_taptest_table_p50000_p51000
     id_taptest_table_p50000_p52000
     id_taptest_table_p50000_p53000
     id_taptest_table_p50000_p54000
     id_taptest_table_p50000_p55000
     id_taptest_table_p50000_p56000
     id_taptest_table_p50000_p57000
     id_taptest_table_p50000_p58000
     id_taptest_table_p50000_p59000
     id_taptest_table_p60000
     id_taptest_table_p60000_p60000
     id_taptest_table_p60000_p61000
     id_taptest_table_p60000_p62000
     id_taptest_table_p60000_p63000
     id_taptest_table_p60000_p64000
     id_taptest_table_p60000_p65000
     id_taptest_table_p60000_p66000
     id_taptest_table_p60000_p67000
     id_taptest_table_p60000_p68000
     id_taptest_table_p60000_p69000
     id_taptest_table_p70000
     id_taptest_table_p70000_p70000
     id_taptest_table_p70000_p71000
     id_taptest_table_p70000_p72000
     id_taptest_table_p70000_p73000
     id_taptest_table_p70000_p74000
     id_taptest_table_p70000_p75000
     id_taptest_table_p70000_p76000
     id_taptest_table_p70000_p77000
     id_taptest_table_p70000_p78000
     id_taptest_table_p70000_p79000
     id_taptest_table_p80000
     id_taptest_table_p80000_p80000
     id_taptest_table_p80000_p81000
     id_taptest_table_p80000_p82000
     id_taptest_table_p80000_p83000
     id_taptest_table_p80000_p84000
     id_taptest_table_p80000_p85000
     id_taptest_table_p80000_p86000
     id_taptest_table_p80000_p87000
     id_taptest_table_p80000_p88000
     id_taptest_table_p80000_p89000
     id_taptest_table_p90000
     id_taptest_table_p90000_p90000
     id_taptest_table_p90000_p91000
     id_taptest_table_p90000_p92000
     id_taptest_table_p90000_p93000
     id_taptest_table_p90000_p94000
     id_taptest_table_p90000_p95000
     id_taptest_table_p90000_p96000
     id_taptest_table_p90000_p97000
     id_taptest_table_p90000_p98000
     id_taptest_table_p90000_p99000
    (119 rows)
```
We can still take this another level deeper as well. Normally with a large amount of data, it's not recommended to partition down to an interval this low since the benefit gained is minimal compared the management of such a large number of tables. But it's being done here as an example. Just as with the time example above, we now have to sub-partition each one of the sub-parent tables to say how we want their children sub-partitioned:
```
    SELECT partman.create_sub_parent('partman_test.id_taptest_table_p0', 'col1', 'partman', '100', p_jobmon := false, p_premake := 2);
    SELECT partman.create_sub_parent('partman_test.id_taptest_table_p10000', 'col1', 'partman', '100', p_jobmon := false, p_premake := 2);
    SELECT partman.create_sub_parent('partman_test.id_taptest_table_p20000', 'col1', 'partman', '100', p_jobmon := false, p_premake := 2);
    SELECT partman.create_sub_parent('partman_test.id_taptest_table_p30000', 'col1', 'partman', '100', p_jobmon := false, p_premake := 2);
    SELECT partman.create_sub_parent('partman_test.id_taptest_table_p40000', 'col1', 'partman', '100', p_jobmon := false, p_premake := 2);
    SELECT partman.create_sub_parent('partman_test.id_taptest_table_p50000', 'col1', 'partman', '100', p_jobmon := false, p_premake := 2);
    SELECT partman.create_sub_parent('partman_test.id_taptest_table_p60000', 'col1', 'partman', '100', p_jobmon := false, p_premake := 2);
    SELECT partman.create_sub_parent('partman_test.id_taptest_table_p70000', 'col1', 'partman', '100', p_jobmon := false, p_premake := 2);
    SELECT partman.create_sub_parent('partman_test.id_taptest_table_p80000', 'col1', 'partman', '100', p_jobmon := false, p_premake := 2);
    SELECT partman.create_sub_parent('partman_test.id_taptest_table_p90000', 'col1', 'partman', '100', p_jobmon := false, p_premake := 2);
    SELECT partman.create_sub_parent('partman_test.id_taptest_table_p100000', 'col1', 'partman', '100', p_jobmon := false, p_premake := 2);
```
I won't show the full list here, but you can see how every child table of the above parents is now a parent table itself with the appropriate minimal child table created where needed as well as the child tables around the current max:
```
    keith@keith=# SELECT tablename FROM pg_tables WHERE schemaname = 'partman_test' order by tablename;
                    tablename                 
    ------------------------------------------
     id_taptest_table
     id_taptest_table_p0
     id_taptest_table_p0_p0
     id_taptest_table_p0_p0_p0
     id_taptest_table_p0_p1000
     id_taptest_table_p0_p1000_p1000
     id_taptest_table_p0_p2000
     id_taptest_table_p0_p2000_p2000
     ...
     id_taptest_table_p10000
     id_taptest_table_p100000
     id_taptest_table_p100000_p100000
     id_taptest_table_p100000_p100000_p100000
     id_taptest_table_p100000_p100000_p100100
     id_taptest_table_p100000_p100000_p100200
     id_taptest_table_p100000_p101000
     id_taptest_table_p100000_p101000_p101000
     id_taptest_table_p100000_p102000
     id_taptest_table_p100000_p102000_p102000
     id_taptest_table_p10000_p10000
     id_taptest_table_p10000_p10000_p10000
     id_taptest_table_p10000_p11000
     id_taptest_table_p10000_p11000_p11000
     ...
     id_taptest_table_p90000_p98000
     id_taptest_table_p90000_p98000_p98000
     id_taptest_table_p90000_p99000
     id_taptest_table_p90000_p99000_p99800
     id_taptest_table_p90000_p99000_p99900
    (225 rows)
```
If you ran the check_parent() function, you'd see that now each one of these new parent tables now needs to have its data moved. Now's a good time show a trick for generating many individual statements based on values returned from a query:
```
    SELECT 'python partition_data.py -c host=localhost -p '||parent_table||' -t id -i 100' FROM partman.part_config ORDER BY parent_table;

                                                    ?column?                                                 
    ---------------------------------------------------------------------------------------------------------
     python partition_data.py -c host=localhost -p partman_test.id_taptest_table -t id -i 100
     python partition_data.py -c host=localhost -p partman_test.id_taptest_table_p0 -t id -i 100
     python partition_data.py -c host=localhost -p partman_test.id_taptest_table_p0_p0 -t id -i 100
     python partition_data.py -c host=localhost -p partman_test.id_taptest_table_p0_p1000 -t id -i 100
    ...
```
This will generate the commands to partition out the data found in any parent table managed by pg_partman. Yes some are already empty, but that won't matter since they'll just do nothing and it makes the query to generate these commands easier. Recommend putting the output from this into an executable shell file vs just pasting it all into the shell directly. Now if you get a list of all the tables, you can see there's quite a lot now (the row count returned is the number of tables).
```
    keith@keith=# SELECT tablename FROM pg_tables WHERE schemaname = 'partman_test' order by tablename;
                    tablename                
    -----------------------------------------
     id_taptest_table
     id_taptest_table_p0
     id_taptest_table_p0_p0
     id_taptest_table_p0_p0_p0
     id_taptest_table_p0_p0_p100
     id_taptest_table_p0_p0_p200
     id_taptest_table_p0_p0_p300
     id_taptest_table_p0_p0_p400
     id_taptest_table_p0_p0_p500
     id_taptest_table_p0_p0_p600
     id_taptest_table_p0_p0_p700
     id_taptest_table_p0_p0_p800
     id_taptest_table_p0_p0_p900
     id_taptest_table_p0_p1000
     id_taptest_table_p0_p1000_p1000
     id_taptest_table_p0_p1000_p1100
     id_taptest_table_p0_p1000_p1200
     id_taptest_table_p0_p1000_p1300
     id_taptest_table_p0_p1000_p1400
     id_taptest_table_p0_p1000_p1500
     id_taptest_table_p0_p1000_p1600
     id_taptest_table_p0_p1000_p1700
     id_taptest_table_p0_p1000_p1800
     id_taptest_table_p0_p1000_p1900
     id_taptest_table_p0_p2000
     id_taptest_table_p0_p2000_p2000
     id_taptest_table_p0_p2000_p2100
     ...
     id_taptest_table_p90000_p98000_p98800
     id_taptest_table_p90000_p98000_p98900
     id_taptest_table_p90000_p99000
     id_taptest_table_p90000_p99000_p99000
     id_taptest_table_p90000_p99000_p99100
     id_taptest_table_p90000_p99000_p99200
     id_taptest_table_p90000_p99000_p99300
     id_taptest_table_p90000_p99000_p99400
     id_taptest_table_p90000_p99000_p99500
     id_taptest_table_p90000_p99000_p99600
     id_taptest_table_p90000_p99000_p99700
     id_taptest_table_p90000_p99000_p99800
     id_taptest_table_p90000_p99000_p99900
    (1124 rows)
```
Now all 100,000 rows are properly partitioned where they should be and any new rows should go where they're supposed to. 


### Set run_maintenance() to run often enough

Using the above time-based partitions, run_maintenance() should be called at least twice a day to ensure it keeps up with the requirements of the smallest time partition interval (daily).

For serial based partitioning, you must know your data ingestion rate and call it often enough to keep new partitions created ahead of that rate.

If you're using the Background Worker (BGW), set the pg_partman_bgw.interval value in postgresql.conf. This example sets it to run every 12 hrs (43200 seconds). See the doc/pg_partman.md file for more information on the BGW settings.

    pg_partman_bgw.interval = 43200
    pg_partman_bgw.role = 'keith'
    pg_partman_bgw.dbname = 'keith'

If you're not using the BGW, you must use a third-party scheduling tool like cron to schedule the calls to run_maintenance()

    03 01,13 * * * psql -c "SELECT run_maintenance()"

### Use Retention Policy

To drop partitions on the first example above that are older than 30 days, set the following:
```
    UPDATE part_config SET retention = '30 days', retention_keep_table = false WHERE parent_table = 'partman_test.time_taptest_table';
```
To drop partitions on the second example above that contain a value 100 less than the current max (max(col1) - 100), set the following:
```
    UPDATE part_config SET retention = '100', retention_keep_table = false WHERE parent_table = 'partman_test.id_taptest_table';
```
For example, once the current id value of col1 reaches 1000, all partitions with values less than 900 will be dropped.

If you'd like to keep the old data stored offline in dump files, set the retention_schema column as well (the keep* config options will be overridden if this is set):
```
    UPDATE part_config SET retention = '30 days', retention_schema = 'archive' WHERE parent_table = 'partman_test.time_taptest_table';
```
Then use the included python script **dump_partition.py** to dump out all tables contained in the archive schema:
```
    $ python dump_partition.py -c "host=localhost username=postgres" -d mydatabase -n archive -o /path/to/dump/location 
```
To implement any retention policy, just ensure run_maintenance() is called often enough for your needs. That function handles both partition creation and the retention policies.


### Undo Partitioning: Simple Time Based

As with partitioning data out, it's best to use the python script to undo partitioning as well to avoid contention and moving large amounts of data in a single transaction. Except for the final example, there's no data in these partition sets, but the example would work either way. This also shows how you can give time-based partition sets a lower interval than what they are partitioned at. This set was daily above, but the batches are committed at the hourly marks (if there was data).
```
    $ python undo_partition.py -p partman_test.time_taptest_table -c host=localhost -t time -i "1 hour"
    Attempting to turn off autovacuum for partition set...
        ... Success!
    Total rows moved: 0
    Running vacuum analyze on parent table...
    Attempting to reset autovacuum for old parent table...
        ... Success!
```
### Undo Partitioning: Simple Serial ID

This just undoes the id partitions committing at the default partition interval of 10 given above.
```
    $ python undo_partition.py -p partman_test.id_taptest_table -c host=localhost -t id
    Attempting to turn off autovacuum for partition set...
        ... Success!
    Total rows moved: 0
    Running vacuum analyze on parent table...
    Attempting to reset autovacuum for old parent table...
        ... Success!
```

### Undo Partitioning: Sub-partition ID->ID->ID

Undoing sub-partitioning involves a little more work (or possibly a lot if it's a large set). You have to start from the bottom up. Just as I did above for generating statements for partitioning the data out, I can do the same for the undo_partition.py script. Keep in mind this gets the undo statement for ALL the parents at once. You do have to go through and parse out the top level calls as well as the mid-level partition, but this at least saves you a lot of potential typing (and typos). The bottom partitions must all be done first and the top last. Also, in this case I have no intention of keeping the old, empty tables anymore, so the --droptable option is given. pg_partman tries to be as safe as possible, so it only uninherits tables by default when undoing partitioning. If you want something dropped, you have to be sure and tell it.
```
    SELECT 'python undo_partition.py -c host=localhost -p '||parent_table||' -t id -i 100 --droptable' FROM partman.part_config ORDER BY parent_table;
```
First do the lowest level sub-partitions:
```
    python undo_partition.py -c host=localhost -p partman_test.id_taptest_table_p0_p0 -t id -i 100 --droptable
    python undo_partition.py -c host=localhost -p partman_test.id_taptest_table_p0_p1000 -t id -i 100 --droptable
    python undo_partition.py -c host=localhost -p partman_test.id_taptest_table_p0_p2000 -t id -i 100 --droptable
    ...
    python undo_partition.py -c host=localhost -p partman_test.id_taptest_table_p100000_p100000 -t id -i 100 --droptable
    python undo_partition.py -c host=localhost -p partman_test.id_taptest_table_p100000_p101000 -t id -i 100 --droptable
    python undo_partition.py -c host=localhost -p partman_test.id_taptest_table_p100000_p102000 -t id -i 100 --droptable
```
Next do what were the mid level sub-partitions:
```
    python undo_partition.py -c host=localhost -p partman_test.id_taptest_table_p0 -t id -i 100 --droptable
    python undo_partition.py -c host=localhost -p partman_test.id_taptest_table_p10000 -t id -i 100 --droptable
    python undo_partition.py -c host=localhost -p partman_test.id_taptest_table_p100000 -t id -i 100 --droptable
    python undo_partition.py -c host=localhost -p partman_test.id_taptest_table_p110000 -t id -i 100 --droptable
    python undo_partition.py -c host=localhost -p partman_test.id_taptest_table_p120000 -t id -i 100 --droptable
    python undo_partition.py -c host=localhost -p partman_test.id_taptest_table_p20000 -t id -i 100 --droptable
    python undo_partition.py -c host=localhost -p partman_test.id_taptest_table_p30000 -t id -i 100 --droptable
    python undo_partition.py -c host=localhost -p partman_test.id_taptest_table_p40000 -t id -i 100 --droptable
    python undo_partition.py -c host=localhost -p partman_test.id_taptest_table_p50000 -t id -i 100 --droptable
    python undo_partition.py -c host=localhost -p partman_test.id_taptest_table_p60000 -t id -i 100 --droptable
    python undo_partition.py -c host=localhost -p partman_test.id_taptest_table_p70000 -t id -i 100 --droptable
    python undo_partition.py -c host=localhost -p partman_test.id_taptest_table_p80000 -t id -i 100 --droptable
    python undo_partition.py -c host=localhost -p partman_test.id_taptest_table_p90000 -t id -i 100 --droptable
```
And finally do the last, top level partition:
```
    python undo_partition.py -c host=localhost -p partman_test.id_taptest_table -t id -i 100 --droptable
```
Now there is only one table left with all the data
```
    keith@keith=# SELECT tablename FROM pg_tables WHERE schemaname = 'partman_test' ORDER BY tablename;
        tablename    
    -----------------
     id_taptest_table

    keith@keith=# SELECT count(*) FROM partman_test.id_taptest_table ;
     count  
    --------
     100000
    (1 row)
```
### Undo Partitioning: Sub-partition Time->Time->Time

This is done in the same exact way as for ID->ID->ID except the undo_partition.py script would use the -t time setting and -i would use a time interval value.


Hopefully these working examples can help you get started. Again, please see the `pg_partman.md` doc for the full details on all the functions and features of this extension. If you have any issues or questions, feel free to open an issue on the github page: https://github.com/pgpartman/pg_partman
