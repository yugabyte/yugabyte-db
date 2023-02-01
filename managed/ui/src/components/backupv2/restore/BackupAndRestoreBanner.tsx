/*
 * Created on Tue Aug 16 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import { useSelector } from 'react-redux';
import { BACKUP_IN_PROGRESS_MSG, RESTORE_IN_PROGRESS_MSG } from '../common/BackupUtils';

export const BackupAndRestoreBanner = () => {
  const currentUniverse = useSelector((state: any) => state.universe.currentUniverse);

  if (!currentUniverse) {
    return null;
  }

  const {
    universeDetails: { updateInProgress, updatingTask }
  } = currentUniverse.data;

  if (updateInProgress) {
    if (updatingTask === 'CreateBackup') {
      return BACKUP_IN_PROGRESS_MSG;
    }
    if (updatingTask === 'RestoreBackup') {
      return RESTORE_IN_PROGRESS_MSG;
    }
  }

  return null;
};
