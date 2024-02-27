export interface ReplicationSlot {
  slotName: string;
  databaseName: string;
  state: string;
  streamID: string;
}

export interface ReplicationSlotResponse {
  replicationSlots: ReplicationSlot[];
}

export interface metricsResponse {
  streamID: string;
  cdcsdk_sent_lag_micros: Record<string, any>;
}
