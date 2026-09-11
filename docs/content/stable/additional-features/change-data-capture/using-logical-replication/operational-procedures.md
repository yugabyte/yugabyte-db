---
title: Operational Procedures for Logical Replication CDC
headerTitle: Operational procedures
linkTitle: Operational procedures
description: Step-by-step procedures for managing active CDC deployments.
headcontent: Safe DDL operations, slot management, and troubleshooting
aliases:
  - /stable/explore/change-data-capture/using-logical-replication/operational-procedures/
menu:
  stable:
    parent: explore-change-data-capture-logical-replication
    identifier: operational-procedures
    weight: 40
type: docs
---

## Before You Start

**Prerequisites:**
- Review [Setup and Configuration](./setup-configuration/) - all gflags configured
- Have CDC consumers ready to handle schema changes
- Understand your replica identity settings
- Know your retention window (default: 8 hours)

## Safe DDL Operations During Active CDC

### Adding a Column to a Replicated Table

**Use case:** Extend existing table schema without disrupting CDC

**Procedure:**

1. **Notify consumers** that a new column is coming
   - Kafka Connect: No action needed if using SMT transformations
   - Custom consumers: Ensure they handle unknown columns gracefully

2. **Execute ALTER TABLE on YugabyteDB**
   ```sql
   ALTER TABLE orders ADD COLUMN shipping_method VARCHAR(50) DEFAULT 'standard';
   ```

3. **Verify column is visible** in CDC output
   ```sql
   -- Check table structure
   \d orders
   
   -- Verify via replication (consumer should see new column in next change)
   ```

4. **Monitor consumer** for any errors
   - If consumer crashes: update consumer schema and restart
   - If consumer ignores column: expected behavior, can process later

**Timing:** Column available to consumers immediately (no restart needed in v2026.1+)

### Dropping a Column from a Replicated Table

**Use case:** Remove a column while CDC is streaming

**Procedure:**

1. **Notify consumers** that column will be removed
   - Kafka Connect: Update SMT if it references this column
   - Custom consumers: Prepare to handle missing column

2. **Execute ALTER TABLE on YugabyteDB**
   ```sql
   ALTER TABLE orders DROP COLUMN legacy_field;
   ```

3. **Verify consumer** handles missing column without errors
   - Kafka messages no longer include this column
   - Consumers should silently ignore missing columns

**Important:** Consumer must not have hard dependency on column existing

### Modifying Column Type (Table Rewrite)

**Use case:** Change column data type (e.g., INT → BIGINT)

#### For Non-Colocated Tables (v2026.1+)

**Non-blocking procedure (slot does NOT need to be dropped):**

1. **Execute ALTER TABLE**
   ```sql
   ALTER TABLE users ALTER COLUMN age TYPE BIGINT;
   ```

2. **CDC automatically detects rewrite**
   - Sends DDL event to consumer
   - Completes streaming old table data
   - Transitions to new table's tablets
   - No slot interruption

3. **Consumer receives:**
   - DDL event: `ALTER TABLE users ALTER COLUMN age TYPE BIGINT`
   - Continues with new schema
   - No data loss

**No action needed** - CDC handles transparently!

#### For Colocated Tables (Always Blocked)

**Blocking procedure (must drop slot):**

1. **Check if table is colocated**
   ```sql
   SELECT tablename, colocated FROM pg_tables WHERE tablename = 'users';
   ```

2. **Stop CDC consumer**
   ```bash
   # For Kafka Connect:
   curl -X DELETE http://localhost:8083/connectors/my-connector
   ```

3. **Drop replication slot**
   ```sql
   DROP REPLICATION SLOT slot_name;
   ```

4. **Perform the DDL**
   ```sql
   ALTER TABLE users ALTER COLUMN age TYPE BIGINT;
   ```

5. **Recreate slot and restart consumer**
   ```sql
   CREATE REPLICATION SLOT slot_name FOR PUBLICATION pub_name USING cdcsdk;
   ```
   ```bash
   # Restart Kafka Connect connector
   curl -X POST -H "Content-Type: application/json" \
     -d @connector-config.json \
     http://localhost:8083/connectors
   ```

{{< warning title="Warning" >}}
Dropping and recreating a slot causes loss of consumer state. You'll need to:
- Re-snapshot all tables
- Replay or skip duplicate records
- Coordinate with downstream systems
{{< /warning >}}

### Renaming a Column

**Use case:** Rename a column while CDC is active

**Procedure:**

1. **Rename column**
   ```sql
   ALTER TABLE orders RENAME COLUMN order_date TO created_at;
   ```

2. **Consumers see new column name** in CDC output immediately

3. **No restart needed** - CDC handles transparently

## Publication Management

### Adding a New Table to an Existing Publication

**Use case:** Start replicating a new table without disrupting existing ones

#### For v2026.1 (Implicit Publication Changes)

**Automatic detection (no refresh wait):**

1. **Add table to publication**
   ```sql
   ALTER PUBLICATION my_pub ADD TABLE new_table;
   ```

2. **CDC automatically detects** within 1 second
   - No consumer restart needed
   - Snapshot begins immediately
   - New table events arrive within seconds

3. **Verify in consumer**
   ```bash
   # For Kafka Connect, check topics:
   kafka-topics.sh --list
   # Should see topic for new_table
   ```

**No additional steps needed.**

#### For v2025.1-v2025.2 (Periodic Refresh)

**Refresh interval-based detection:**

1. **Add table to publication**
   ```sql
   ALTER PUBLICATION my_pub ADD TABLE new_table;
   ```

2. **Wait for refresh interval** (default: 900 seconds / 15 minutes)
   ```bash
   # Check when next refresh will happen:
   # Refresh times: 8:00, 8:15, 8:30, 8:45...
   # If you add table at 8:01, it will be detected at 8:15
   ```

3. **To speed up detection**, temporarily lower refresh interval:
   ```bash
   # On each YB-TServer:
   yb-ts-cli --server_address=<tserver-ip:9100> \
     set_flag cdcsdk_publication_list_refresh_interval_secs 60
   
   # Wait for table to be detected (check consumer logs)
   
   # Restore original interval:
   yb-ts-cli --server_address=<tserver-ip:9100> \
     set_flag cdcsdk_publication_list_refresh_interval_secs 900
   ```

**Note:** Every refresh has overhead. Lower interval only temporarily.

### Removing a Table from Publication

**Use case:** Stop replicating a table without affecting other tables

**Procedure:**

1. **Verify table is in the publication**
   ```sql
   SELECT * FROM pg_publication_tables WHERE tablename = 'old_table';
   ```

2. **Check if it's in other publications** (if not, safe to drop)
   ```sql
   SELECT pubname FROM pg_publication_rel 
   WHERE relid = 'old_table'::regclass;
   ```

3. **Remove from publication**
   ```sql
   ALTER PUBLICATION my_pub DROP TABLE old_table;
   ```

4. **Verify consumer stops receiving events** for this table
   - New changes no longer appear in CDC output
   - Existing buffered changes may still arrive briefly

**No consumer restart needed.**

### Adding to "ALL TABLES" Publication

**Use case:** Table is created after publication, should auto-appear

**Procedure:**

1. **Publication already exists**
   ```sql
   SELECT * FROM pg_publication WHERE pubname = 'my_pub';
   -- Should show pubcorelation '0' (meaning all tables)
   ```

2. **Create new table**
   ```sql
   CREATE TABLE analytics (id INT PRIMARY KEY, event_type TEXT);
   ```

3. **Verify table is automatically included** (v2026.1+)
   ```sql
   SELECT * FROM pg_publication_tables WHERE pubname = 'my_pub';
   -- Should include analytics table
   ```

4. **Consumer automatically** receives new table's snapshot and events

**For v2025.1-v2025.2:** Wait for refresh interval for table to be detected.

## Replication Slot Management

### Monitoring Slot Status

**Check all active slots:**

```sql
SELECT 
  slot_name,
  slot_type,
  active,
  restart_lsn,
  confirmed_flush_lsn,
  pg_wal_lsn_diff(pg_current_wal_lsn(), restart_lsn) AS lag_bytes
FROM pg_replication_slots;
```

**Interpret results:**
- `active = true`: Consumer currently reading from slot
- `lag_bytes`: How far behind consumer is (should be < 1 GB in production)
- If `lag_bytes > retention window`: Slot will become unusable when lag exceeds retention

### Safely Recreating a Slot

**Use case:** Reset slot without losing consumer position, enable parallel testing

**Procedure (Parallel Slot Strategy):**

1. **Create new slot** (slot_v2)
   ```sql
   CREATE REPLICATION SLOT slot_v2 FOR PUBLICATION my_pub USING cdcsdk;
   ```

2. **Configure consumer to use slot_v2**
   - Update Kafka Connect connector config
   - Update custom consumer code
   - **Do NOT stop old consumer yet**

3. **Start consuming from slot_v2**
   - Both slots consuming simultaneously
   - Old slot catches up to current position

4. **Monitor both slots until caught up**
   ```sql
   SELECT slot_name, restart_lsn, pg_current_wal_lsn(),
     pg_wal_lsn_diff(pg_current_wal_lsn(), restart_lsn) AS lag
   FROM pg_replication_slots
   WHERE slot_name IN ('slot_v1', 'slot_v2');
   ```

5. **When slot_v2 is caught up:**
   - Verify consumer has processed all events
   - Optionally switch back if needed for continuity

6. **Drop old slot**
   ```sql
   DROP REPLICATION SLOT slot_v1;
   ```

**Benefits:**
- Zero downtime
- Can verify new slot is working before cutting over
- Old slot remains available for rollback

### Recovering from Unusable Slot

**Symptoms:**
- "replication slot is unusable" error
- Consumer disconnects with "WAL not available" message
- Slot created but never receives data

**Root causes:**
1. WAL retention exceeded (consumer fell behind > retention window)
2. Invalid table in publication
3. Slot corruption

**Diagnosis:**

```sql
-- Check slot status
SELECT slot_name, slot_type, active FROM pg_replication_slots;

-- If not there, slot is dead

-- Check what tables are in publication:
SELECT * FROM pg_publication_tables WHERE pubname = 'my_pub';

-- Check for invalid tables:
SELECT tablename, schemaname FROM pg_tables 
WHERE tablename IN (SELECT tablename FROM pg_publication_tables);
```

**Recovery:**

1. **Identify the issue**
   ```sql
   -- If publication has a dropped table:
   SELECT * FROM pg_publication_rel WHERE NOT EXISTS 
     (SELECT 1 FROM pg_class WHERE oid = pg_publication_rel.relid);
   ```

2. **Fix the publication** (if table issue)
   ```sql
   -- Remove problematic table from publication
   ALTER PUBLICATION my_pub DROP TABLE dropped_table;
   ```

3. **Drop the unusable slot**
   ```sql
   DROP REPLICATION SLOT unusable_slot;
   ```

4. **Recreate slot and consumer**
   ```sql
   CREATE REPLICATION SLOT slot_new FOR PUBLICATION my_pub USING cdcsdk;
   ```
   ```bash
   # Restart consumer (will snapshot tables again)
   ```

5. **Verify new slot works**
   ```sql
   SELECT * FROM pg_replication_slots WHERE slot_name = 'slot_new';
   -- Should show active = false initially, then true when consumer connects
   ```

**Note:** New slot requires full snapshot (no offset continuation).

### Monitoring Slot Lag Over Time

**Track lag history:**

```bash
# Create monitoring script
while true; do
  echo "$(date): $(psql -c "
    SELECT pg_wal_lsn_diff(pg_current_wal_lsn(), restart_lsn) / 1024 / 1024 AS lag_mb
    FROM pg_replication_slots
    WHERE slot_name = 'my_slot';
  " -t)"
  sleep 60
done
```

**Set up alerting:**
- Alert if lag exceeds 500 MB
- Alert if lag > 80% of retention window
- Alert if lag increases steadily (consumer can't keep up)

## Handling Common Issues

### Slot Becomes Unusable After Consumer Restart

**Symptom:** Consumer restarted, slot shows "inactive", reconnect fails

**Cause:** Consumer couldn't reconnect within grace period

**Solution:**

```sql
-- Check slot status
SELECT * FROM pg_replication_slots WHERE slot_name = 'my_slot';

-- If inactive, consumer is disconnected but slot still valid
-- Consumer should auto-reconnect when restarted

-- If getting "unusable" error, check consumer logs for reason
-- Reasons:
-- - Invalid LSN position
-- - Slot dropped and recreated with same name
-- - Permission issues
```

### Consumer Processing Events Very Slowly

**Symptom:** Lag keeps growing, retention window may be exceeded

**Root causes:**
1. Consumer is I/O bound (writing to slow storage)
2. Batch size too large (tuning issue)
3. Downstream system (Kafka, database) is slow

**Diagnosis:**

```bash
# Check consumer-side metrics
# For Kafka Connect:
curl http://localhost:8083/connectors/my-connector/status

# For custom consumer:
# Check application logs for processing time
```

**Solutions:**

1. **Increase batch size** (if not already tuned):
   ```bash
   # On YB-TServer:
   set_flag cdcsdk_max_consistent_records 1000  # Increase from default 100
   ```

2. **Optimize downstream writes** (Kafka, database)
   - Add parallelism
   - Batch writes
   - Check network latency

3. **Monitor retention** - if consumer will fall behind:
   ```sql
   -- Increase retention as temporary measure
   -- But FIX the consumer slowness!
   ALTER SYSTEM SET cdc_wal_retention_time_secs = 86400;  -- 24 hours
   SELECT pg_reload_conf();
   ```

### Read Latency Degraded After Enabling CDC

**Symptom:** Normal queries become slow when CDC is active

**Root cause:** Replica identity set to FULL, compaction halted

**Solution:**

```sql
-- Check replica identity
SELECT relname, relreplident FROM pg_class 
WHERE relname IN ('orders', 'users');

-- If relreplident = 'f' (FULL), consider changing:
ALTER TABLE orders REPLICA IDENTITY USING INDEX orders_pkey;

-- Verify change:
SELECT relname, relreplident FROM pg_class WHERE relname = 'orders';
```

**If must use FULL:**
- Ensure consumer is fast (< 1 second lag)
- Monitor read latency continuously
- Consider scaling up read replicas
- Enable read-only mode on replicas if possible

### "Publication contains an unreplicated table"

**Symptom:** Error when trying to create slot for publication

**Cause:** Publication includes a table that doesn't support CDC (system table, etc.)

**Solution:**

```sql
-- Check all tables in publication:
SELECT * FROM pg_publication_tables WHERE pubname = 'my_pub';

-- Remove tables that don't support CDC:
ALTER PUBLICATION my_pub DROP TABLE system_table;

-- Recreate slot:
CREATE REPLICATION SLOT my_slot FOR PUBLICATION my_pub USING cdcsdk;
```

