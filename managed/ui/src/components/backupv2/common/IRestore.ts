import { IBackup } from "./IBackup";

export enum Restore_States {
  IN_PROGRESS = 'InProgress',
  COMPLETED = 'Completed',
  FAILED = 'Failed',
  ABORTED = 'Aborted'
}
export interface IRestore {
  createTime: string;
  updateTime: string;
  totalBackupSizeInBytes?: number;
  isSourceUniversePresent: boolean;
  sourceUniverseUUID: string;
  universeUUID: string;
  state: Restore_States;
  targetUniverseName: string;
  sourceUniverseName: string;
  restoreSizeInBytes: number;
  backupType?: IBackup['backupType']
  backupCreatedOnDate?: string;
  restoreKeyspaceList: {
    targetKeyspace: string;
    storageLocation: string;
    tableNameList?: string[];
  }[]
}

export const RESTORE_STATUS_OPTIONS: { value: Restore_States | null; label: string }[] = [
  {
    label: 'All',
    value: null
  },
  {
    label: 'In Progress',
    value: Restore_States.IN_PROGRESS
  },
  {
    label: 'Completed',
    value: Restore_States.COMPLETED
  },
  {
    label: 'Failed',
    value: Restore_States.FAILED
  }
];
