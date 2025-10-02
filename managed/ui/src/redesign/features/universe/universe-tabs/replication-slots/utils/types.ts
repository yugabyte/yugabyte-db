export enum SlotState {
  INITIATED = 'INITIATED',
  ACTIVE = 'ACTIVE',
  DELETING = 'DELETING',
  DELETED = 'DELETED',
  DELETING_METADATA = 'DELETING_METADATA',
  EXPIRED = 'EXPIRED'
}

export interface ReplicationSlot {
  slotName: string;
  databaseName: string;
  state: SlotState;
  streamID: string;
}

export interface ReplicationSlotResponse {
  replicationSlots: ReplicationSlot[];
}

export interface metricsResponse {
  streamID: string;
  cdcsdk_flush_lag: Record<string, any>;
  cdcsdk_expiry_time_mins: Record<string, any>;
  cdcsdk_change_event_count: Record<string, any>;
  cdcsdk_traffic_sent: Record<string, any>;
}
