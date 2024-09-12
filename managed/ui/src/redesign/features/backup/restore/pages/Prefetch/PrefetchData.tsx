/*
 * Created on Fri Aug 23 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { forwardRef, useImperativeHandle } from 'react';
import { useQuery } from 'react-query';
import { useMount } from 'react-use';
import { useTranslation } from 'react-i18next';
import { noop } from 'lodash';
import { useFormContext } from 'react-hook-form';
import { Page, PageRef, RestoreContextMethods } from '../../models/RestoreContext';
import { YBLoadingCircleIcon } from '../../../../../../components/common/indicators';
import { fetchIncrementalBackup } from '../../../../../../components/backupv2/common/BackupAPI';
import { RestoreFormModel } from '../../models/RestoreFormModel';
import { Backup_States } from '../../../../../../components/backupv2';
import { GetRestoreContext } from '../../RestoreUtils';

// The prefetch Data Component is used to fetch the incremental backups data, if present.
const PrefetchData = forwardRef<PageRef>((_, forwardRef) => {
  const [
    { backupDetails, additionalBackupProps },
    { moveToPage, setIncrementalBackupsData }
  ]: RestoreContextMethods = GetRestoreContext();

  const { setValue } = useFormContext<RestoreFormModel>();

  useImperativeHandle(forwardRef, () => ({ onNext: noop, onPrev: noop }), []);

  const { t } = useTranslation();

  useQuery(
    ['incremental_backups', backupDetails?.commonBackupInfo.baseBackupUUID],
    () => fetchIncrementalBackup(backupDetails!.commonBackupInfo?.baseBackupUUID),
    {
      enabled:
        backupDetails !== null &&
        backupDetails.hasIncrementalBackups &&
        (additionalBackupProps?.isRestoreEntireBackup ||
          !additionalBackupProps?.singleKeyspaceRestore),
      onSuccess(data) {
        // set incremental backups data in context
        setIncrementalBackupsData(data.data);
        // if restore entire backup is selected, then set the latest completed incremental backup as the restore point.
        // if single keyspace restore is selected, then set the incremental backup selected(from BackupDetails) as the restore point.
        // if incremental backup is selected, the search for that id and set it.
        const incrementalBackup = additionalBackupProps?.incrementalBackupUUID
          ? data.data.find((ic) => ic.backupUUID === additionalBackupProps?.incrementalBackupUUID) // restore to the point
          : data.data.filter((ic) => ic.state === Backup_States.COMPLETED)[0];
        if (incrementalBackup) {
          setValue('currentCommonBackupInfo', incrementalBackup);
        }
        moveToPage(Page.SOURCE);
      }
    }
  );

  useMount(() => {
    // if the backup doesn't have incremental backups, move to next page.
    if (!backupDetails?.hasIncrementalBackups) {
      setValue('currentCommonBackupInfo', backupDetails!.commonBackupInfo);
      moveToPage(Page.SOURCE);
    }

    // If single keyspace inside incremental backups is restored, we already have the necessary information for restore.
    // so, set storage location as response list and move to next page.
    if (
      additionalBackupProps?.singleKeyspaceRestore &&
      additionalBackupProps.incrementalBackupUUID
    ) {
      setValue('currentCommonBackupInfo', backupDetails!.commonBackupInfo);
      moveToPage(Page.SOURCE);
    }
  });
  return (
    <>
      <YBLoadingCircleIcon />
      <div style={{ textAlign: 'center' }}>{t('newRestoreModal.prefetch.loadingText')}</div>
    </>
  );
});

PrefetchData.displayName = 'PrefetchData';
export default PrefetchData;
