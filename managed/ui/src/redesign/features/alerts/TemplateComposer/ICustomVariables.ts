/*
 * Created on Mon Feb 20 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

export interface SystemVariables {
  name: string;
  description: string;
}
export interface CustomVariable {
  uuid?: string;
  name: string;
  possibleValues: string[];
  defaultValue: string;
}

export type AlertVariableType = 'CUSTOM' | 'SYSTEM';

export interface IAlertVariablesList {
  systemVariables: SystemVariables[];
  customVariables: CustomVariable[];
}

const AlertTemplateList = [
  'REPLICATION_LAG',
  'CLOCK_SKEW',
  'MEMORY_CONSUMPTION',
  'HEALTH_CHECK_ERROR',
  'HEALTH_CHECK_NOTIFICATION_ERROR',
  'UNIVERSE_METRIC_COLLECTION_FAILURE',
  'BACKUP_FAILURE',
  'BACKUP_SCHEDULE_FAILURE',
  'INACTIVE_CRON_NODES',
  'ALERT_QUERY_FAILED',
  'ALERT_CONFIG_WRITING_FAILED',
  'ALERT_NOTIFICATION_ERROR',
  'ALERT_NOTIFICATION_CHANNEL_ERROR',
  'NODE_DOWN',
  'NODE_RESTART',
  'NODE_CPU_USAGE',
  'NODE_DISK_USAGE',
  'NODE_FILE_DESCRIPTORS_USAGE',
  'NODE_OOM_KILLS',
  'DB_VERSION_MISMATCH',
  'DB_INSTANCE_DOWN',
  'DB_INSTANCE_RESTART',
  'DB_FATAL_LOGS',
  'DB_ERROR_LOGS',
  'DB_CORE_FILES',
  'DB_YSQL_CONNECTION',
  'DB_YCQL_CONNECTION',
  'DB_REDIS_CONNECTION',
  'DB_MEMORY_OVERLOAD',
  'DB_COMPACTION_OVERLOAD',
  'DB_QUEUES_OVERFLOW',
  'DB_DRIVE_FAILURE',
  'DB_WRITE_READ_TEST_ERROR',
  'NODE_TO_NODE_CA_CERT_EXPIRY',
  'NODE_TO_NODE_CERT_EXPIRY',
  'CLIENT_TO_NODE_CA_CERT_EXPIRY',
  'CLIENT_TO_NODE_CERT_EXPIRY',
  'ENCRYPTION_AT_REST_CONFIG_EXPIRY',
  'SSH_KEY_EXPIRY',
  'SSH_KEY_ROTATION_FAILURE',
  'PITR_CONFIG_FAILURE',
  'YSQL_OP_AVG_LATENCY',
  'YCQL_OP_AVG_LATENCY',
  'YSQL_OP_P99_LATENCY',
  'YCQL_OP_P99_LATENCY',
  'HIGH_NUM_YSQL_CONNECTIONS',
  'HIGH_NUM_YCQL_CONNECTIONS',
  'HIGH_NUM_YEDIS_CONNECTIONS',
  'YSQL_THROUGHPUT',
  'YCQL_THROUGHPUT',
  'MASTER_LEADER_MISSING',
  'MASTER_UNDER_REPLICATED',
  'LEADERLESS_TABLETS',
  'UNDER_REPLICATED_TABLETS',
  'PRIVATE_ACCESS_KEY_STATUS'
] as const;

/**
 * Source: src/main/java/com/yugabyte/yw/models/AlertConfiguration.java
 */
export const AlertThresholdSeverity = {
  SEVERE: 'SEVERE',
  WARNING: 'WARNING'
} as const;
export type AlertThresholdSeverity = typeof AlertThresholdSeverity[keyof typeof AlertThresholdSeverity];

/**
 * Source: src/main/java/com/yugabyte/yw/models/common/Condition.java
 */
export const AlertThresholdCondition = {
  GREATER_THAN: 'GREATER_THAN',
  LESS_THAN: 'LESS_THAN',
  NOT_EQUAL: 'NOT_EQUAL'
};
export type AlertThresholdCondition = typeof AlertThresholdCondition[keyof typeof AlertThresholdCondition];

export interface IAlertConfiguration {
  uuid: string;
  customerUUID: string;
  name: string;
  description: string;
  createTime: string;
  targetType: 'PLATFORM' | 'UNIVERSE';
  target: {
    all?: boolean;
    uuids?: string[];
  };
  thresholds: {
    [key in AlertThresholdSeverity]?: {
      condition: AlertThresholdCondition;
      threshold: number;
    };
  };
  thresholdUnit: string;
  template: typeof AlertTemplateList[number];
  durationSec: number;
  active: boolean;
  defaultDestination: boolean;
  labels?: Record<string, string>;
}

export type IAlertConfigurationList = IAlertConfiguration[];

export interface IAlertChannelTemplates {
  type: 'Email' | 'WebHook';
  titleTemplate?: string;
  textTemplate?: string;
  defaultTextTemplate?: string;
  defaultTitleTemplate?: string;
  highlightedTitleTemplate?: string;
  highlightedTextTemplate?: string;
}
