export enum Restore_States {
  IN_PROGRESS = 'InProgress',
  COMPLETED = 'Completed',
  FAILED = 'Failed',
  ABORTED = 'Aborted'
}
export interface IRestore {
  createTime: number;
  totalBackupSizeInBytes?: number;
  universeUUID: string;
  state: Restore_States;
  targetUniverseName: string;
  sourceUniverseName: string;
  restoreSizeInBytes: number;
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
  },
  {
    label: 'Aborted',
    value: Restore_States.ABORTED
  }
];
