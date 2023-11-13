---
title: Alert policy templates
headerTitle: Alert policy templates
linkTitle: Alert policy templates
description: Alert policy template reference
menu:
  v2.18_yugabyte-platform:
    identifier: alert-policy-templates
    parent: set-up-alerts-health-checking
    weight: 40
type: docs
---

[Alert policies](../set-up-alerts-health-check/#manage-alert-policies) use the following templates to define how the alert is triggered. The alert templates have been created using Prometheus expressions.

### DB CQLSH connection

CQLSH connection failure has been detected for universe `'$universe_name'` on `$value` T-Server instances.

```expression
ybp_health_check_cqlsh_connectivity_error{universe_uuid="$uuid"} > 0
```

### DB compaction overload

Database compaction rejections detected for universe `'$universe_name'`.

```expression
sum by (node_prefix) (increase(majority_sst_files_rejections{node_prefix="$node_prefix"}[10m])) > 0
```

### DB write/read test error

Test YSQL write/read operation failed on `$value` nodes for universe `'$universe_name'`.

```expression
count by (node_prefix) (yb_node_ysql_write_read{node_prefix="$node_prefix"} < 1)
```

### DB version mismatch

Version mismatch has been detected for universe `'$universe_name'` for `$value` Master or T-Server instances.

```expression
ybp_health_check_tserver_version_mismatch{universe_uuid="$uuid"} + ybp_health_check_master_version_mismatch{universe_uuid="$uuid"} > 0
```

### Client to node cert expiry

Client to node certificate for universe `'$universe_name'` expires in `$value` days.

```expression
min by (node_name) (ybp_health_check_c2n_cert_validity_days{universe_uuid="$uuid"} < 30)
```

### Health check notification error

Failed to issue health check notification for universe `'$universe_name'`. You need to check Health notification settings and YugabyteDB Anywhere logs for details or contact {{% support-platform %}}.

```expression
last_over_time(ybp_health_check_notification_status{universe_uuid = "$uuid"}[1d]) < 1
```

### Health check error

Failed to perform health check for universe `'$universe_name'`. You need to check YugabyteDB Anywhere logs for details or contact {{% support-platform %}}.

```expression
last_over_time(ybp_health_check_status{universe_uuid = "$uuid"}[1d]) < 1
```

### Encryption at rest config expiry

Encryption at rest configuration for universe `'$universe_name'` expires in `$value` days.

```expression
ybp_universe_encryption_key_expiry_days{universe_uuid="$uuid"} < 3
```

### DB Redis connection

Redis connection failure has been detected for universe `'$universe_name'` on `$value` T-Server instances.

```expression
ybp_health_check_redis_connectivity_error{universe_uuid="$uuid"} > 0
```

### Backup schedule failure

Last attempt to run a scheduled backup for universe `'$universe_name'` failed due to other backup or universe operation in progress.

```expression
last_over_time(ybp_schedule_backup_status{universe_uuid = "$uuid"}[1d]) < 1
```

### Node to node cert expiry

Node to node certificate for universe `'$universe_name'` expires in `$value` days.

```expression
min by (node_name) (ybp_health_check_n2n_cert_validity_days{universe_uuid="$uuid"} < 30)
```

### DB core files

Core files detected for universe `'$universe_name'` on `$value` T-Server instances.

```expression
ybp_health_check_tserver_core_files{universe_uuid="$uuid"} > 0
```

### Alert notification failed

Last attempt to send alert notifications for customer `'yugabyte support'` failed. You need to check YugabyteDB Anywhere logs for details or contact {{% support-platform %}}.

```expression
last_over_time(ybp_alert_manager_status{customer_uuid = "$uuid"}[1d]) < 1
```

### Backup failure

Last backup task for universe `'$universe_name'` failed. You need to check the backup task result for details.

```expression
last_over_time(ybp_create_backup_status{universe_uuid = "$uuid"}[1d]) < 1
```

### Under-replicated tablets

`$value` tablets remain under-replicated for more than 5 minutes in universe `'$universe_name'`.

```expression
max by (node_prefix) (count by (node_prefix, exported_instance) (max_over_time(yb_node_underreplicated_tablet{node_prefix="$node_prefix"}[5m])) > 0)
```

### DB memory overload

Database memory rejections have been detected for universe `'$universe_name'`.

```expression
sum by (node_prefix) (increase(leader_memory_pressure_rejections{node_prefix="$node_prefix"}[10m])) + sum by (node_prefix) (increase(follower_memory_pressure_rejections{node_prefix="$node_prefix"}[10m])) + sum by (node_prefix) (increase(operation_memory_pressure_rejections{node_prefix="$node_prefix"}[10m])) > 0
```

### Client to node CA cert expiry

Client to node CA certificate for universe `'$universe_name'` expires in `$value` days.

```expression
min by (node_name) (ybp_health_check_c2n_ca_cert_validity_days{universe_uuid="$uuid"} < 30)
```

### DB queues overflow

Database queues overflow has been detected for universe `'$universe_name'`.

```expression
sum by (node_prefix) (increase(rpcs_queue_overflow{node_prefix="$node_prefix"}[10m])) + sum by (node_prefix) (increase(rpcs_timed_out_in_queue{node_prefix="$node_prefix"}[10m])) > 1
```

### Alert rules sync failed

Last alert rules synchronization for customer `'yugabyte support'` has failed. YugabyteDB Anywhere logs for details or contact {{% support-platform %}}.

```expression
last_over_time(ybp_alert_config_writer_status[1d]) < 1
```

### Alert query failed

Last alert query for customer `'yugabyte support'` failed. YugabyteDB Anywhere logs for details or contact {{% support-platform %}}.

```expression
last_over_time(ybp_alert_query_status[1d]) < 1
```

### DB fatal logs

Fatal logs have been detected for universe `'$universe_name'` on `$value` Master or T-Server instances.

```expression
sum by (universe_uuid) (ybp_health_check_node_master_fatal_logs{universe_uuid="$uuid"} < bool 1) + sum by (universe_uuid) (ybp_health_check_node_tserver_fatal_logs{universe_uuid="$uuid"} < bool 1) > 0
```

### Master leader missing

Master leader is missing for universe `'$universe_name'`.

```expression
max by (node_prefix) (yb_node_is_master_leader{node_prefix="$node_prefix"}) < 1
```

### DB node restart

Universe `'$universe_name'` database node has restarted `$value` times during last 30 minutes.

```expression
max by (node_prefix) (changes(node_boot_time{node_prefix="$node_prefix"}[30m])) > 0
```

### Node to node CA cert expiry

Node to node CA certificate for universe `'$universe_name'` expires in `$value` days.

```expression
min by (node_name) (ybp_health_check_n2n_ca_cert_validity_days{universe_uuid="$uuid"} < 30)
```

### DB instance restart

Universe `'$universe_name'` Master or T-Server has restarted `$value` times during last 30 minutes.

```expression
max by (node_prefix) (changes(yb_node_boot_time{node_prefix="$node_prefix"}[30m]) and on (node_prefix) (max_over_time(ybp_universe_update_in_progress{node_prefix="$node_prefix"}[31m]) == 0)) > 0
```

### DB node OOM

More than one out of memory (OOM) kills have been detected for universe `'$universe_name'` on `$value` nodes.

```expression
count by (node_prefix) (yb_node_oom_kills_10min{node_prefix="$node_prefix"} > 1) > 0
```

### DB node down

`$value` database nodes are down for more than 15 minutes for universe `'$universe_name'`.

```expression
count by (node_prefix) (max_over_time(up{export_type="node_export",node_prefix="$node_prefix"}[15m]) < 1) > 0
```

### DB node file descriptors usage

Node file descriptors usage for universe `'$universe_name'` is above 70% on `$value` nodes.

```expression
count by (universe_uuid) (ybp_health_check_used_fd_pct{universe_uuid="$uuid"} > 70)
```

### Alert channel failed

Last attempt to send alert notifications to channel `'{{ $labels.source_name }}'` has failed. You need to try sending a test alert to obtain details.

```expression
last_over_time(ybp_alert_manager_channel_status{customer_uuid = "$uuid"}[1d]) < 1
```

### DB node CPU usage

Average node CPU usage for universe `'$universe_name'` is more than 90% on `$value` nodes.

```expression
count by(node_prefix) ((100 - (avg by (node_prefix, instance) (avg_over_time(irate(node_cpu_seconds_total{job="node",mode="idle", node_prefix="$node_prefix"}[1m])[30m:])) * 100)) > 90)
```

### DB instance down

`$value` database Master or T-Server instances are down for more than 15 minutes for universe `'$universe_name'`.

```expression
count by (node_prefix) (label_replace(max_over_time(up{export_type=~"master_export|tserver_export",node_prefix="$node_prefix"}[15m]), "exported_instance", "$1", "instance", "(.*)") < 1 and on (node_prefix, export_type, exported_instance) (min_over_time(ybp_universe_node_function{node_prefix="$node_prefix"}[15m]) == 1)) > 0
```

### Inactive cronjob nodes

`$value` nodes have inactive cronjob for universe `'$universe_name'`.

```expression
ybp_universe_inactive_cron_nodes{universe_uuid = "$uuid"} > 0
```

### DB node disk usage

Node disk usage for universe `'$universe_name'` is more than 70% on `$value` nodes.

```expression
count by (node_prefix) (100 - (sum without (saved_name) (node_filesystem_free_bytes{mountpoint=~"/mnt/.*", node_prefix="$node_prefix"}) / sum without (saved_name) (node_filesystem_size_bytes{mountpoint=~"/mnt/.*", node_prefix="$node_prefix"}) * 100) > 70)
```

### Clock skew

Maximum clock skew for universe `'$universe_name'` is more than 500 milliseconds. The current value is `$value` milliseconds.

```expression
max by (node_prefix) (max_over_time(hybrid_clock_skew{node_prefix="$node_prefix"}[10m])) / 1000 > 500
```

### Leaderless tablets

The tablet leader is missing for more than 5 minutes for `$value` tablets in universe `'$universe_name'`.

```expression
max by (node_prefix) (count by (node_prefix, exported_instance) (max_over_time(yb_node_leaderless_tablet{node_prefix="$node_prefix"}[5m])) > 0)
```

<!--

### Command to obtain information

You can obtain information for alert templates from YugabyteDB Anywhere by executing the following command:

```sh
aakra01@UCTDES01 ~ k exec yb-support-platform-yugaware-0 -c prometheus -n yb-platform -- ls /opt/yugabyte/prometheus/rules |more -5 yugaware.ad.034b2eec-23c6-4ef2-8a25-aab9820a4086.yml yugaware.ad.04b30133-e62d-44ef-a7c1-7aadb6bf4c39.yml yugaware.ad.0d28c7a4-e44f-4247-8343-029662075233.yml yugaware.ad.155697f8-f928-40a3-98e5-78f0b645c4f6.yml yugaware.ad.18175790-3fae-4008-83a8-41bb221a2cf7.yml --More--   aakra01@UCTDES01 ~ k get configmap/yb-support-platform-yugaware-prometheus-config -n yb-platform -o yaml |grep -i rule    rule_files:    - '/opt/yugabyte/prometheus/rules/yugaware.ad.*.yml'
```

-->
