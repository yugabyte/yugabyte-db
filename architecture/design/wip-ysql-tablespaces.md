# YSQL Tablespaces

## Background
YSQL Tablespaces are entities that specify how data associated with them should be replicated and distributed across cloud/regions/zones. Historically, Tablespaces are PostgreSQL entities that allow specifying a location on disk with options that control how the data can be accessed. Tablespaces thus allow grouping of tables/indexes based on the way a user wants to store/access the data present in them. For instance, in PostgreSQL, heavily accessed smaller tables/indexes can be placed in SSD or “fast” storage compared to other tables.

For Yugabyte clusters however, location does not pertain to “disk locations”. For a cloud native distributed database, location pertains to the cloud/region/zone where the data should reside. Therefore, although YSQL Tablespaces build on PostgreSQL Tablespaces and allow specifying placement of data at a table level, in YSQL, this placement information specifies the number of replicas for a table/index, and how they can be distributed across a set of cloud/region/zones. 

## Creating Tablespaces
Tablespaces can be created in YSQL using the following API:

```sql
CREATE TABLESPACE tablespace_name
    [ OWNER { user_name | CURRENT_USER | SESSION_USER } ]
    [ LOCATION 'ignored_value' ]
    [ WITH ( replica_placement = placement_policy_json ) ]
```
Parameters:

**tablespace_name**: The name of the tablespace to be created.

**user_name:** The name of the user who will own the tablespace. If omitted, defaults to the user executing the command. Only superusers can create tablespaces, but they can assign ownership of tablespaces to non-superusers.

**placement_policy_json:** This is a json string that specifies the placement policy for this tablespace. The json structure contains two fields “num_replicas” and “placement_blocks”. “num_replicas” specifies the overall replication factor. “placement_blocks” is an array of tuples, where each tuple contains the keys <”cloud”, “region”, “zone” and “min_num_replicas”>. The values for these keys define a placement block. 
Typically the sum of “min_num_replicas” across all the placement blocks is expected to be equal to “num_replicas”. The aggregate of “min_num_replicas” can be lesser than “num_replicas”; if so, the “extra” replicas will be placed at the YB-Load balancer’s discretion. This is elucidated in later examples.

**ignored_value:** The LOCATION field is still accepted only to maintain parity with PostgreSQL for customers migrating from PostgreSQL. However this value will be discarded and not used. If a user does not specify “replica_placement” for a tablespace, then associating the tablespace with a table/index will not affect how the data of the table will be placed.       

### Examples

```sql
CREATE TABLESPACE test_tablespace WITH (replica_placement='{"num_replicas": 3, "placement_blocks":[{"cloud":"cloud1","region":"r1","zone":"z1","min_num_replicas":1}, {"cloud":"cloud2","region":"r2","zone":"z2","min_num_replicas":1}, {"cloud":"cloud3","region":"r3","zone":"z3","min_num_replicas":1} ]}');
```

Using “test_tablespace” created above while creating a table will result in each tablet of the table having 3 replicas (value of “num_replicas”). Since “placement_blocks” contains 3 placement blocks where the min_num_replicas = 1, YB-Load balancer will ensure that cloud1.r1.z1, cloud2.r2.z2 and cloud3.r3.z3 will each have 1 replica.
Now let us consider an example where the min_num_replicas do not sum up to the “num_replicas”:

```sql
CREATE TABLESPACE test_tablespace2 WITH (replica_placement='{"num_replicas": 5, "placement_blocks":[{"cloud":"cloud1","region":"r1","zone":"z1","min_num_replicas":1}, {"cloud":"cloud2","region":"r2","zone":"z2","min_num_replicas":1}, {"cloud":"cloud3","region":"r3","zone":"z3","min_num_replicas":1} ]}');

NOTICE:  num_replicas is 5, and the total min_num_replicas fields is 3. The location of the additional 2 replicas among the specified zones will be decided dynamically based on the cluster load
```

As seen above, the creation of the tablespace is successful, but the statement throws a NOTICE stating that the additional 2 replicas will be placed at the Yugabyte Load Balancer’s discretion among cloud1.r1.z1, cloud2.r2.z2 and cloud3.r3.z3. Based on the load, cloud1.r1.z1 could have 3 replicas whereas the other zones have 1 each. Or it could later change such that cloud1.r1.z1 and cloud2.r2.z2 each have 2 replicas whereas cloud3.r3.z3 has only 1. Thus the LB will always honor the “min_num_replicas” field and have at least 1 replica in each cloud.region.zone, whereas the additional replicas may be moved around based on the cluster load.

Setting the LOCATION field will work, but will throw a warning:
```sql
CREATE TABLESPACE x LOCATION '/data';
WARNING:  LOCATION not supported yet and will be ignored
LINE 1: CREATE TABLESPACE x LOCATION '/data';
                            ^
HINT:  See https://github.com/YugaByte/yugabyte-db/issues/6569. Click '+' on the description to raise its priority
```

As mentioned above, this is allowed only to maintain parity with PostgreSQL API and will not have any advantage in Yugabyte clusters.
**Note:** YSQL will only check whether the placement policy is syntactically valid. If there are no TServers in the cloud/region/zones specified, the tablespace creation will NOT fail. However, associating tables/indexes with them will fail.

## Creating Tables/Indexes in Tablespaces
While creating tables/indexes, they can be easily associated with a corresponding tablespace. This will dictate the replication factor of the table/index and how the replicas of the table/index will be spread across cloud/region/zones.

The existing YSQL CREATE TABLE and CREATE INDEX statements will now additionally support the TABLESPACE option alongside them. Example YSQL statements are as follows:

```sql
CREATE TABLE yb_table (a int, b text) TABLESPACE us_west_tablespace;
CREATE INDEX a_idx ON yb_table(a) TABLESPACE us_east_tablespace;
```

The above statements will ensure that the data in tables “yb_table” and “a_idx” will be present based on the placement policies specified for “us_west_tablespace” and “us_east_tablespace” respectively.

**Note:**
* By default, when no tablespaces have been specified, the placement policy for a table/index will ALWAYS default to the current cluster configuration. If the cluster configuration changes, the placement of the table/index will also change.
* As shown above, it is not necessary for a table and its indexes to belong to the same Tablespace. If a custom tablespace has been specified for a table, but not for its indexes, the indexes will be placed based on the cluster configuration, i.e. an index will NOT INHERIT the tablespace from the table being indexed.
* If a partitioned table has a tablespace associated with it, as of today, this will NOT be the default tablespace for the child partitions. This will be fixed in the future and is tracked by #7293
* As mentioned above, if there aren’t sufficient TServers to satisfy the placement policies specified in the tablespace, the table/index creation will fail. However, if creation succeeds and later TServers later fail, these tables will remain in an under-replicated state until there are new TServers such that the tablespace placement policy can be satisfied. 

## Drop Tablespace
The syntax to drop a tablespace is as follows:

```sql
DROP TABLESPACE [ IF EXISTS ] name
```
**Parameters:**

**name:** Denotes the name of the tablespace to be dropped.

If there are tables/indexes still associated with the tablespace to be dropped, then the above command will fail:

```sql
CREATE TABLE us_west_users(userid int, username text) TABLESPACE us_west;

DROP TABLESPACE us_west;
ERROR:  tablespace "us_west" cannot be dropped because some objects depend on it
DETAIL:  tablespace of table us_west_users
```

## Backup and Restore With Tablespaces
With YSQL tables, the usual method to perform backup and restore of the data in YB cluster is to use ysql_dump (to dump a particular database) or ysql_dumpall (to dump cross database entities like roles and call ysql_dump for each database.) Tablespaces are cross-database entities, hence to dump tablespace creation commands, we would need to use ysql_dumpall.
ysql_dump will dump the creation statements of tables/indexes preserving the tablespace that they were created in.

### Sample ysql_dump/ysql_dumpall commands
The default command "ysql_dumpall" will dump all the tablespaces, roles and database related commands. Ysql_dumpall will call “ysql_dump” on each database in the cluster, and “ysql_dump” will dump table/index creation commands that are associated with the respective tablespace
#### Dump only tablespace creation commands. 
ysql_dumpall -t
ysql_dumpall --tablespaces-only 

#### Ask ysql_dump to ignore tablespace assignments for tables and indexes
ysql_dump --no-tablespaces 

**Note:** If the cluster to which the backup is happening does not have the same placement zones and regions configured, then you may need to edit the tablespace creation commands dumped by ysql_dumpall to reflect the placement policies of the new cluster. Otherwise table creation will fail in the cluster where we are restoring data.

## Row Level Geo Partitioning and Tablespaces
Tablespaces will help complete the story on row level geo partitioning. They can be used in conjunction with table partitioning to specify the location of data at a row level. Table partitioning helps split the table at a row level into multiple tables, and using a tablespace to pin each such partition to a separate geo location will help achieve row level geo partitioning. This is elucidated by the simple example given below:

### Example usage
First, lets create tablespaces. Each tablespace pertains to a specific region in the US:

```sql

CREATE TABLESPACE us_west_tablespace WITH (replica_placement=' {"num_replicas":3,"placement_blocks":[ {"cloud":"aws","region":"us-west","zone":"us-west-1a","min_num_replicas":1}, {"cloud":"aws","region":"us-west","zone":"us-west-1b","min_num_replicas":1}, {"cloud":"aws","region":"us-west","zone":"us-west-1c","min_num_replicas":1}]}');

CREATE TABLESPACE us_east_tablespace WITH (replica_placement=' {"num_replicas":3,"placement_blocks":[ {"cloud":"aws","region":"us-east","zone":"us-east-1a","min_num_replicas":1}, {"cloud":"aws","region":"us-east","zone":"us-east-1b","min_num_replicas":1}, {"cloud":"aws","region":"us-east","zone":"us-east-1c","min_num_replicas":1}]}');

CREATE TABLESPACE us_central_tablespace WITH (replica_placement=' {"num_replicas":3,"placement_blocks":[ {"cloud":"aws","region":"us-central","zone":"us-central-1a","min_num_replicas":1}, {"cloud":"aws","region":"us-central","zone":"us-central-1b","min_num_replicas":1}, {"cloud":"aws","region":"us-central","zone":"us-central-1c","min_num_replicas":1}]}');

```
Next, create a partitioned table:

```sql
CREATE TABLE car(car_id   INTEGER NOT NULL,
    make_and_model VARCHAR NOT NULL,
    city_location VARCHAR,
    booking_status BOOLEAN,
    available_at TIMESTAMP DEFAULT NOW(),
    miles INTEGER NOT NULL,
    geo_partition VARCHAR NOT NULL,
    PRIMARY KEY(car_id, geo_partition),
    PARTITION BY LIST(geo_partition) SPLIT INTO 1 TABLETS;
```
The above table is partitioned based on the column "geo-partition". Now create child partitions:

```sql
CREATE TABLE car_us_west PARTITION OF car FOR VALUES IN ('us-west') TABLESPACE us_west_tablespace;

CREATE TABLE car_us_east PARTITION OF car FOR VALUES IN ('us-east') TABLESPACE us_east_tablespace;

CREATE TABLE car_us_central PARTITION OF car FOR VALUES IN ('us-central') TABLESPACE us_central_tablespace;
```

Now any data inserted into “Car” will be split based on the value specified for the “geo-partition” column. Based on the geo-partition value for the row inserted into “Car”, the row will be found in the corresponding US-region.

Let us insert a few rows into ‘Car’:

```sql
INSERT INTO car VALUES (1, 'Honda Accord', 'Sunnyvale', false, NOW(), 0, 'us-west');
INSERT INTO car VALUES (2, 'Nissan Altima', 'Miami', false, NOW(), 0, 'us-east');
INSERT INTO car VALUES (3, 'Toyota Corolla', 'Chicago', false, NOW(), 0, 'us-central');
INSERT INTO car VALUES (4, 'Nissan Sentra', 'Missouri', false, NOW(), 0, 'us-central');
INSERT INTO car VALUES (5, 'Toyota Camry', 'Seattle', false, NOW(), 0, 'us-west');
INSERT INTO car VALUES (6, 'Honda Civic', 'New York City', false, NOW(), 0, 'us-east');
```

Check that the rows indeed made their way into the expected child partitions:
```sql
SELECT tableoid::regclass, car_id, city_location, geo_partition FROM car ORDER BY car_id;
    tableoid    | car_id | city_location | geo_partition 
----------------+--------+---------------+---------------
 car_us_west    |      1 | Sunnyvale     | us-west
 car_us_east    |      2 | Miami         | us-east
 car_us_central |      3 | Chicago       | us-central
 car_us_central |      4 | Missouri      | us-central
 car_us_west    |      5 | Seattle       | us-west
 car_us_east    |      6 | New York City | us-east
(6 rows)
```

As can be seen above, the rows inserted into the table “Car” were transparently split into the partition tables ‘car_us_west’, ‘car_us_central’ and ‘car_us_east’. 

View the query plan:
```sql
yugabyte=# EXPLAIN SELECT * FROM car WHERE geo_partition='us-west' AND city_location='Sunnyvale';
                                    QUERY PLAN                                                  
-------------------------------------------------------------------------------------------
 Append  (cost=0.00..110.00 rows=1000 width=85)
   ->  Seq Scan on car_us_west  (cost=0.00..105.00 rows=1000 width=85)
         Filter: (((geo_partition)::text = 'us-west'::text) AND ((city_location)::text = 'Sunnyvale'::text))
(3 rows)
```

Thus, the query planner sees that the WHERE clause would only match one of the partition tables, and the sequential scan is performed only on car-us-west. This way, all the queries on table ‘car’ will only query data from the region closest to them, thus providing good performance.

## Future Work
In later releases we will support the following features:
* Support for ALTER TABLE SET TABLESPACE
* Support for ALTER TABLESPACE
* Support setting read-replica placements and affinitized leaders using Tablespaces
* Support setting tablespace for a Database
* Support setting tablespace for a Tablegroup
* Explore supporting additional options that allow attributes other than cloud location. For example, something similar to pinning a tablespace only to SSD disks etc

## Conclusion
Thus in the above way, tablespaces can be used to specify the location of a YSQL table. They also help complete the story for row level geo partitioning by providing an SQL interface to pin the partition tables to required locations.
