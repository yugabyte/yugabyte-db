import moment from 'moment';

/**
 * Extracts the timestamp from a platform HA backup file name.
 * Backup filenames are in the format of backup_YYYY-MM-DD-HH-mm.tgz. Ex. backup_2025-01-01-00-00.tgz.
 * Returns the extracted timestamp.
 */
export const getHaBackupFileTimestamp = (backupFileName: string) =>
  backupFileName.replace('backup_', '').replace('.tgz', '');

export const HA_BACKUP_OLD_THRESHOLD_HOURS = 24;
/**
 * Checks if a given timestamp is older than HA_BACKUP_OLD_THRESHOLD_HOURS from the current time.
 * True if the timestamp is older than HA_BACKUP_OLD_THRESHOLD_HOURS, false otherwise.
 */
export const getIsHaBackupOld = (backupFileName: string): boolean => {
  const backupTimestamp = moment.utc(getHaBackupFileTimestamp(backupFileName), 'YYYY-MM-DD-HH-mm');
  const now = moment.utc();
  const hoursDiff = now.diff(backupTimestamp, 'hours');

  return hoursDiff > HA_BACKUP_OLD_THRESHOLD_HOURS;
};
