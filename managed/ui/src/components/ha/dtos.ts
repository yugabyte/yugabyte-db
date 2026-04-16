/**
 * A global state for the YBA HA configuration.
 * This state is determined by the current instance's relationship with
 * all other instances in the HA configuration.
 *
 * Source: managed/src/main/java/com/yugabyte/yw/common/HaConfigStates.java
 *
 * TODO: Check if we're intentionally returning the enum name instead of the enum value.
 */
export const HaGlobalState = {
  UNKNOWN: 'Unknown',
  NO_REPLICAS: 'NoReplicas',
  AWAITING_REPLICAS: 'AwaitingReplicas',
  OPERATIONAL: 'Operational',
  ERROR: 'Error',
  WARNING: 'Warning',
  STANDBY_CONNECTED: 'StandbyConnected',
  STANDBY_DISCONNECTED: 'StandbyDisconnected'
} as const;
export type HaGlobalState = typeof HaGlobalState[keyof typeof HaGlobalState];

/**
 * A per instance state specific to a particular YBA platform instance.
 *
 * Source: managed/src/main/java/com/yugabyte/yw/common/HaConfigStates.java
 */
export const HaInstanceState = {
  AWAITING_REPLICAS: 'Awaiting Connection to Replicas',
  CONNECTED: 'Connected',
  DISCONNECTED: 'Disconnected'
} as const;
export type HaInstanceState = typeof HaInstanceState[keyof typeof HaInstanceState];

export interface HaPlatformInstance {
  address: string;
  config_uuid: string;
  instance_state: HaInstanceState;
  is_leader: boolean;
  is_local: boolean;
  last_backup: string | null;
  uuid: string;
}

export interface HaConfig {
  // `accept_any_certificate` determines whether backend will perform cert validation
  accept_any_certificate: boolean;
  cluster_key: string;
  global_state: HaGlobalState;
  instances: HaPlatformInstance[];
  last_failover: number;
  uuid: string;
}

export interface HaReplicationSchedule {
  frequency_milliseconds: number;
  is_running: boolean;
}
