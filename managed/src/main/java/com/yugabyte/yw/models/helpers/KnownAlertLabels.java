/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.models.helpers;

public enum KnownAlertLabels {
  CONFIGURATION_UUID,
  CONFIGURATION_TYPE,
  ALERT_UUID,
  ALERT_TYPE,
  DEFINITION_UUID,
  DEFINITION_NAME,
  UNIVERSE_UUID,
  UNIVERSE_NAME,
  CUSTOMER_UUID,
  CUSTOMER_NAME,
  CUSTOMER_CODE,
  SOURCE_UUID,
  SOURCE_NAME,
  SOURCE_TYPE,
  SNAPSHOT_UUID,
  TABLE_TYPE,
  NAMESPACE_NAME,
  PITR_CONFIG_UUID,
  ALERT_STATE,
  SEVERITY,
  THRESHOLD,
  NODE_NAME,
  NODE_PREFIX,
  INSTANCE,
  EXPORT_TYPE,
  TASK_TYPE,
  RESULT,
  ALERTNAME,
  MESSAGE,
  MAINTENANCE_WINDOW_UUIDS,
  ALERT_EXPRESSION,
  NODE_AGENT_UUID,
  NAMESPACE,
  POD_NAME,
  CONTAINER_NAME,
  NODE_ADDRESS,
  NODE_IDENTIFIER,
  NODE_REGION,
  NODE_CLUSTER_TYPE,
  PARENT_TASK_TYPE,
  YBA_VERSION,
  DB_VERSION,
  TASK_UUID,
  TABLE_UUID,
  SOURCE_UNIVERSE_UUID,
  TARGET_UNIVERSE_UUID,
  XCLUSTER_CONFIG_UUID,
  XCLUSTER_CONFIG_NAME,
  XCLUSTER_REPLICATION_GROUP_NAME;

  public String labelName() {
    return name().toLowerCase();
  }
}
