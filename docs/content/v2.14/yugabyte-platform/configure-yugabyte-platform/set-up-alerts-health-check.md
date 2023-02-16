---
title: Create and configure alerts
headerTitle: Create and configure alerts
linkTitle: Configure alerts
description: Configure alerts and health check
menu:
  v2.14_yugabyte-platform:
    identifier: set-up-alerts-health-checking
    parent: configure-yugabyte-platform
    weight: 40
type: docs
---

YugabyteDB Anywhere can check universes for issues that may affect deployment. Should problems arise, YugabyteDB Anywhere can automatically issue alert notifications

For additional information, see the following:

- [Alerts](../../alerts-monitoring/alert/)
- [Metrics](../../troubleshoot/universe-issues/#use-metrics/)
- [Alerts and Notifications in YugabyteDB Anywhere](https://www.yugabyte.com/blog/yugabytedb-2-8-alerts-and-notifications/)

You can use preconfigured alerts provided by YugabyteDB Anywhere, or create and configure your own alerts based on the metrics' conditions.

You can access YugabyteDB Anywhere health monitor and configure alerts by navigating to **Admin > Alert Configurations**, as per the following illustration:

![Configure alerts](/images/yp/config-alerts1.png)

The **Alert Configurations** view allows you to perform the following for specific universes or for your instance of YugabyteDB Anywhere:

- Create new alert configurations.
- Modify, delete, activate, or deactivate existing alerts, as well as send test alerts via **Actions**.
- Find alerts by applying filters.
- Define maintenance period during which alerts are not issued.

## Create alerts

Regardless of the alert level, you create an alert as follows:

- Navigate to **Alert Configurations > Alert Policies**.

- Click either **Create Alert Config > Universe Alert** or **Create Alert Config > Platform Alert**, depending on the scope of the alert. Note that the scope of **Platform Alert** is YugabyteDB Anywhere.

- Select a template to use, and then configure settings by completing the fields whose default values depend on the template, as per the following illustration:

  ![Create alert](/images/yp/config-alerts2.png)

  Templates are available for alerts related to YugabyteDB Anywhere operations, YugabyteDB operations, as well as YSQL and YCQL performance. For supplemental information on templates, see [Templates reference](#templates-reference).

  Most of the template fields are self-explanatory. The following fields are of note:

  - The **Active** field allows you to define the alert as initially active or inactive.

  - The **Threshold** field allows you to define the value (for example, number of milliseconds, resets, errors, nodes) that must be reached by the metric in order to trigger the alert.

  - The **Destination** field allows you to select one of the previously defined recipients of the alert. For more information, see [Define alert destinations](#define-alert-destinations).

- Click **Save**.

### Templates reference

Alert templates available in YugabyteDB Anywhere have been created using Prometheus expressions. Although not required, you might be inclined to consult the complete list of templates and their corresponding expressions.

#### DB CQLSH connection

CQLSH connection failure has been detected for universe `'$universe_name'` on `$value` T-Server instances.

```expression
ybp_health_check_cqlsh_connectivity_error{universe_uuid="$uuid"} > 0
```

#### DB compaction overload

Database compaction rejections detected for universe `'$universe_name'`.

```expression
sum by (node_prefix) (increase(majority_sst_files_rejections{node_prefix="$node_prefix"}[10m])) > 0
```

#### DB write/read test error

Test YSQL write/read operation failed on `$value` nodes for universe `'$universe_name'`.

```expression
count by (node_prefix) (yb_node_ysql_write_read{node_prefix="$node_prefix"} < 1)
```

#### DB version mismatch

Version mismatch has been detected for universe `'$universe_name'` for `$value` Master or T-Server instances.

```expression
ybp_health_check_tserver_version_mismatch{universe_uuid="$uuid"} + ybp_health_check_master_version_mismatch{universe_uuid="$uuid"} > 0
```

#### Client to node cert expiry

Client to node certificate for universe `'$universe_name'` expires in `$value` days.

```expression
min by (node_name) (ybp_health_check_c2n_cert_validity_days{universe_uuid="$uuid"} < 30)
```

#### Health check notification error

Failed to issue health check notification for universe `'$universe_name'`. You need to check Health notification settings and YugabyteDB Anywhere logs for details or contact the Yugabyte support team.

```expression
last_over_time(ybp_health_check_notification_status{universe_uuid = "$uuid"}[1d]) < 1
```

#### Health check error

Failed to perform health check for universe `'$universe_name'`. You need to check YugabyteDB Anywhere logs for details or contact the Yugabyte support team.

```expression
last_over_time(ybp_health_check_status{universe_uuid = "$uuid"}[1d]) < 1
```

#### Encryption at rest config expiry

Encryption at rest configuration for universe `'$universe_name'` expires in `$value` days.

```expression
ybp_universe_encryption_key_expiry_days{universe_uuid="$uuid"} < 3
```

#### DB Redis connection

Redis connection failure has been detected for universe `'$universe_name'` on `$value` T-Server instances.

```expression
ybp_health_check_redis_connectivity_error{universe_uuid="$uuid"} > 0
```

#### Backup schedule failure

Last attempt to run a scheduled backup for universe `'$universe_name'` failed due to other backup or universe operation in progress.

```expression
last_over_time(ybp_schedule_backup_status{universe_uuid = "$uuid"}[1d]) < 1
```

#### Node to node cert expiry

Node to node certificate for universe `'$universe_name'` expires in `$value` days.

```expression
min by (node_name) (ybp_health_check_n2n_cert_validity_days{universe_uuid="$uuid"} < 30)
```

#### DB core files

Core files detected for universe `'$universe_name'` on `$value` T-Server instances.

```expression
ybp_health_check_tserver_core_files{universe_uuid="$uuid"} > 0
```

#### Alert notification failed

Last attempt to send alert notifications for customer `'yugabyte support'` failed. You need to check YugabyteDB Anywhere logs for details or contact the Yugabyte support team.

```expression
last_over_time(ybp_alert_manager_status{customer_uuid = "$uuid"}[1d]) < 1
```

#### Backup failure

Last backup task for universe `'$universe_name'` failed. You need to check the backup task result for details.

```expression
last_over_time(ybp_create_backup_status{universe_uuid = "$uuid"}[1d]) < 1
```

#### Under-replicated tablets

`$value` tablets remain under-replicated for more than 5 minutes in universe `'$universe_name'`.

```expression
max by (node_prefix) (count by (node_prefix, exported_instance) (max_over_time(yb_node_underreplicated_tablet{node_prefix="$node_prefix"}[5m])) > 0)
```

#### DB memory overload

Database memory rejections have been detected for universe `'$universe_name'`.

```expression
sum by (node_prefix) (increase(leader_memory_pressure_rejections{node_prefix="$node_prefix"}[10m])) + sum by (node_prefix) (increase(follower_memory_pressure_rejections{node_prefix="$node_prefix"}[10m])) + sum by (node_prefix) (increase(operation_memory_pressure_rejections{node_prefix="$node_prefix"}[10m])) > 0
```

#### Client to node CA cert expiry

Client to node CA certificate for universe `'$universe_name'` expires in `$value` days.

```expression
min by (node_name) (ybp_health_check_c2n_ca_cert_validity_days{universe_uuid="$uuid"} < 30)
```

#### DB queues overflow

Database queues overflow has been detected for universe `'$universe_name'`.

```expression
sum by (node_prefix) (increase(rpcs_queue_overflow{node_prefix="$node_prefix"}[10m])) + sum by (node_prefix) (increase(rpcs_timed_out_in_queue{node_prefix="$node_prefix"}[10m])) > 1
```

#### Alert rules sync failed

Last alert rules synchronization for customer `'yugabyte support'` has failed. YugabyteDB Anywhere logs for details or contact the Yugabyte support team.

```expression
last_over_time(ybp_alert_config_writer_status[1d]) < 1
```

#### Alert query failed

Last alert query for customer `'yugabyte support'` failed. YugabyteDB Anywhere logs for details or contact the Yugabyte support team.

```expression
last_over_time(ybp_alert_query_status[1d]) < 1
```

#### DB fatal logs

Fatal logs have been detected for universe `'$universe_name'` on `$value` Master or T-Server instances.

```expression
sum by (universe_uuid) (ybp_health_check_node_master_fatal_logs{universe_uuid="$uuid"} < bool 1) + sum by (universe_uuid) (ybp_health_check_node_tserver_fatal_logs{universe_uuid="$uuid"} < bool 1) > 0
```

#### Master leader missing

Master leader is missing for universe `'$universe_name'`.

```expression
max by (node_prefix) (yb_node_is_master_leader{node_prefix="$node_prefix"}) < 1
```

#### DB node restart

Universe `'$universe_name'` database node has restarted `$value` times during last 30 minutes.

```expression
max by (node_prefix) (changes(node_boot_time{node_prefix="$node_prefix"}[30m])) > 0
```

#### Node to node CA cert expiry

Node to node CA certificate for universe `'$universe_name'` expires in `$value` days.

```expression
min by (node_name) (ybp_health_check_n2n_ca_cert_validity_days{universe_uuid="$uuid"} < 30)
```

#### DB instance restart

Universe `'$universe_name'` Master or T-Server has restarted `$value` times during last 30 minutes.

```expression
max by (node_prefix) (changes(yb_node_boot_time{node_prefix="$node_prefix"}[30m]) and on (node_prefix) (max_over_time(ybp_universe_update_in_progress{node_prefix="$node_prefix"}[31m]) == 0)) > 0
```

#### DB node OOM

More than one out of memory (OOM) kills have been detected for universe `'$universe_name'` on `$value` nodes.

```expression
count by (node_prefix) (yb_node_oom_kills_10min{node_prefix="$node_prefix"} > 1) > 0
```

#### DB node down

`$value` database nodes are down for more than 15 minutes for universe `'$universe_name'`.

```expression
count by (node_prefix) (max_over_time(up{export_type="node_export",node_prefix="$node_prefix"}[15m]) < 1) > 0
```

#### DB node file descriptors usage

Node file descriptors usage for universe `'$universe_name'` is above 70% on `$value` nodes.

```expression
count by (universe_uuid) (ybp_health_check_used_fd_pct{universe_uuid="$uuid"} > 70)
```

#### Alert channel failed

Last attempt to send alert notifications to channel `'{{ $labels.source_name }}'` has failed. You need to try sending a test alert to obtain details.

```expression
last_over_time(ybp_alert_manager_channel_status{customer_uuid = "$uuid"}[1d]) < 1
```

#### DB node CPU usage

Average node CPU usage for universe `'$universe_name'` is more than 90% on `$value` nodes.

```expression
count by(node_prefix) ((100 - (avg by (node_prefix, instance) (avg_over_time(irate(node_cpu_seconds_total{job="node",mode="idle", node_prefix="$node_prefix"}[1m])[30m:])) * 100)) > 90)
```

#### DB instance down

`$value` database Master or T-Server instances are down for more than 15 minutes for universe `'$universe_name'`.

```expression
count by (node_prefix) (label_replace(max_over_time(up{export_type=~"master_export|tserver_export",node_prefix="$node_prefix"}[15m]), "exported_instance", "$1", "instance", "(.*)") < 1 and on (node_prefix, export_type, exported_instance) (min_over_time(ybp_universe_node_function{node_prefix="$node_prefix"}[15m]) == 1)) > 0
```

#### Inactive cronjob nodes

`$value` nodes have inactive cronjob for universe `'$universe_name'`.

```expression
ybp_universe_inactive_cron_nodes{universe_uuid = "$uuid"} > 0
```

#### DB node disk usage

Node disk usage for universe `'$universe_name'` is more than 70% on `$value` nodes.

```expression
count by (node_prefix) (100 - (sum without (saved_name) (node_filesystem_free_bytes{mountpoint=~"/mnt/.*", node_prefix="$node_prefix"}) / sum without (saved_name) (node_filesystem_size_bytes{mountpoint=~"/mnt/.*", node_prefix="$node_prefix"}) * 100) > 70)
```

#### Clock skew

Maximum clock skew for universe `'$universe_name'` is more than 500 milliseconds. The current value is `$value` milliseconds.

```expression
max by (node_prefix) (max_over_time(hybrid_clock_skew{node_prefix="$node_prefix"}[10m])) / 1000 > 500
```

#### Leaderless tablets

The tablet leader is missing for more than 5 minutes for `$value` tablets in universe `'$universe_name'`.

```expression
max by (node_prefix) (count by (node_prefix, exported_instance) (max_over_time(yb_node_leaderless_tablet{node_prefix="$node_prefix"}[5m])) > 0)
```

<!--

#### Command to obtain information

You can obtain information for alert templates from YugabyteDB Anywhere by executing the following command:

```sh
aakra01@UCTDES01 ~ k exec yb-support-platform-yugaware-0 -c prometheus -n yb-platform -- ls /opt/yugabyte/prometheus/rules |more -5 yugaware.ad.034b2eec-23c6-4ef2-8a25-aab9820a4086.yml yugaware.ad.04b30133-e62d-44ef-a7c1-7aadb6bf4c39.yml yugaware.ad.0d28c7a4-e44f-4247-8343-029662075233.yml yugaware.ad.155697f8-f928-40a3-98e5-78f0b645c4f6.yml yugaware.ad.18175790-3fae-4008-83a8-41bb221a2cf7.yml --More--   aakra01@UCTDES01 ~ k get configmap/yb-support-platform-yugaware-prometheus-config -n yb-platform -o yaml |grep -i rule    rule_files:    - '/opt/yugabyte/prometheus/rules/yugaware.ad.*.yml'
```

-->

## Define notification channels

In YugabyteDB Anywhere, a notification channel defines how an alert is issued (via an email, a Slack message, a webhook message, or a PagerDuty message) and who should receive it.

You can create a new channel, as well as modify or delete an existing one as follows:

- Navigate to **Alert Configurations > Notification Channels**, as per the following illustration:

  ![Notification channel](/images/yp/config-alerts7.png)

- To create a new channel, click **Add Channel** and then complete the **Create new alert channel** dialog shown in the following illustration:

  ![New channel](/images/yp/config-alerts6.png)

  If you select **Email** as a notification delivery method, perform the following:

  - Provide a descriptive name for your channel.

  - Use the **Emails** field to enter one or more valid email addresses separated by commas.

  - If you choose to configure the Simple Mail Transfer Protocol (SMTP) settings, toggle the **Custom SMTP Configuration** field and then complete the required fields.

  If you select **Slack** as a notification delivery method, perform the following:

  - Provide a descriptive name for your channel.

  - Use the **Slack Webhook URL** field to enter a valid URL.

  If you select **PagerDuty** as a notification delivery method, perform the following:

  - Provide a descriptive name for your channel.

  - Enter a PagerDuty API key and service integration key.

  If you select **WebHook** as a notification delivery method, perform the following:

  - Provide a descriptive name for your channel.

  - Use the **Webhook URL** field to enter a valid URL.

- To modify an existing channel, click its corresponding **Actions > Edit Channel** and then complete the **Edit alert channel** dialog that has the same fields as the **Create new alert channel** dialog.
- To delete a channel, click **Actions > Delete Channel**.

## Define alert destinations

When an alert is triggered, alert data is sent to a specific alert destination that consists of one or more channels. You can define a new destination for your alerts, view details of an existing destination, edit or delete an existing destination as follows:

- Navigate to **Alert Configurations > Alert Destinations**, as per the following illustration:

  ![Destinations](/images/yp/config-alerts3.png)
- To add a new alert destination, click **Add Destination** and then complete the form shown in the following illustration:

  ![Add destination](/images/yp/config-alerts4.png)

  The preceding form allows you to either select an existing notification channel or create a new one by clicking **Add Channel** and completing the **Create new alert channel** dialog, as described in [Define notification channels](#define-notification-channels).

- Click **Save**.

- To view details, modify, or delete an existing destination, click **Actions** corresponding to this destination and then select either **Channel Details**, **Edit Destination**, or **Delete Destination**.

## Configure heath check

You can define parameters and fine-tune health check that YugabyteDB Anywhere performs on its universes, as follows:

- Navigate to **Alert Configurations > Health** to open the **Alerting controls** view shown in the following illustration:

  ![Health](/images/yp/config-alerts5.png)

- Use the **Alert emails** field to define a comma-separated list of email addresses to which alerts are to be sent.

- Use the **Send alert email to Yugabyte team** field to enable sending the same alerts to Yugabyte Support.

- Use the **Active alert notification interval** field to define the notification period (in milliseconds) for resending notifications for active alerts. The default value of 0 means that only one notification is issued for an active alert.

- Complete the remaining fields or accept the default settings.

- If you enable **Custom SMTP Configuration**, you need to provide the address for the Simple Mail Transfer Protocol (SMTP) server, the port number, the email, the user credentials, and select the desired security settings.

- Click **Save**.

## Configure maintenance periods

You can configure maintenance periods (windows) during which alerts are snoozed by navigating to **Alert Configurations > Maintenance Windows**, as per the following illustration:

![Maintenance](/images/yp/config-alerts9.png)

The preceding view allows you to do the following:

- Extend the maintenance period by clicking **Extend** and selecting the amount of time.

- Mark the maintenance as completed, modify its parameters, or delete it by clicking **Action** and selecting one of the options.

- Add a new maintenance period for all or only specific universes by clicking **Add Maintenance Window** and completing the fields shown in the following illustration:

  ![Maintenance](/images/yp/config-alerts10.png)
