import { TableType } from '../../../../../redesign/helpers/dtos';
import { Backup_States, IBackup } from '../../../common/IBackup';

export const testBackupDetail: Partial<IBackup> = {
  lastIncrementalBackupTime: '2023-03-22T17:03:07Z' as any,
  fullChainSizeInBytes: 0,
  universeName: 'test-lst2',
  isStorageConfigPresent: true,
  isUniversePresent: true,
  universeUUID: '9f98bc1f-d583-4e8a-8061-149fa48448f4',
  customerUUID: '0fccf3ab-500d-4698-a290-ca42e1b3a727',
  hasIncrementalBackups: false,
  lastBackupState: Backup_States.COMPLETED,
  onDemand: true,
  category: 'YB_BACKUP_SCRIPT',
  isFullBackup: false,
  backupType: TableType.PGSQL_TABLE_TYPE,
  commonBackupInfo: {
    state: Backup_States.COMPLETED,
    backupUUID: '4e92bf1e-a965-4666-a180-3cbd1adcc3e2',
    baseBackupUUID: '4e92bf1e-a965-4666-a180-3cbd1adcc3e2',
    storageConfigUUID: '05f67eb9-42ac-4200-96ad-3a5a6d18e8c1',
    taskUUID: 'bdc337e9-cff7-458b-b538-f4e12a5debdf',
    sse: false,
    tableByTableBackup: false,
    createTime: '2023-03-22T17:03:07Z',
    updateTime: '2023-03-22T17:04:35Z',
    completionTime: '2023-03-22T17:03:07Z',
    totalBackupSizeInBytes: 6456556,
    parallelism: 1,
    responseList: [
      {
        keyspace: 'db_lst_029734',
        allTables: false,
        tablesList: [],
        tableUUIDList: [],
        defaultLocation:
          's3://backups.yugabyte.com/s3Backup/univ-9f98bc1f-d583-4e8a-8061-149fa48448f4/backup-2023-03-22T17:03:07-1554712103/keyspace-db_lst_029734'
      }
    ]
  }
};
