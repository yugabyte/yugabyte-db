import moment from 'moment';

import { ContinuousBackup } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { CustomerConfig, CustomerConfigType, StorageConfig } from '../../../components/backupv2';

// Classify the customer config so typescript can infer the correct type.
export const isStorageConfig = (
  customerConfig: CustomerConfig
): customerConfig is StorageConfig => {
  return customerConfig.type === CustomerConfigType.STORAGE;
};

// This type assertion can be removed after updating to Typescript 5.5 as the
// type checker will be able to infer the correct type post version 5.5:
// https://devblogs.microsoft.com/typescript/announcing-typescript-5-5/#inferred-type-predicates
export const getStorageConfigs = (customerConfigs: CustomerConfig[]): StorageConfig[] =>
  customerConfigs.filter((customerConfig) => isStorageConfig(customerConfig)) as StorageConfig[];

const RECENT_BACKUP_THRESHOLD_HOURS = 24;

export const getIsLastPlatformBackupOld = (continuousBackupConfig: ContinuousBackup) => {
  const currentTime = moment();
  const lastBackupTime = continuousBackupConfig.info?.last_backup;

  return (
    !!lastBackupTime && currentTime.diff(lastBackupTime, 'hours') > RECENT_BACKUP_THRESHOLD_HOURS
  );
};
