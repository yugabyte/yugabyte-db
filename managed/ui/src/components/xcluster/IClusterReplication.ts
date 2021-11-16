export interface IReplicationTable {
  tableUUID: string;
  tableName: string;
  tableType: string;
  keySpace: string;
  sizeBytes: string;
}

export enum IReplicationStatus {
  INIT = 'Init',
  SUCCESS = 'Success',
  FAILED = 'Failed',
  PAUSED = 'Paused',
  RUNNING = 'Running'
}

export interface IReplication {
  name: string;
  uuid: string;
  sourceUniverseUUID: string;
  targetUniverseUUID: string;
  masterAddress?: string;
  maxReplicationLagTime: number;
  currentLagTime: number;
  alertIfMaxReplicationLagTimeReached: boolean;
  tables: string[];
  createTime: string;
  modifyTime: string;
  status?: IReplicationStatus;
}
