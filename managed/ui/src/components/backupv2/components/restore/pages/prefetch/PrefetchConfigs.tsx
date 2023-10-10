/*
 * Created on Thu Jul 06 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { useCallback, useContext, useImperativeHandle } from 'react';
import { useQuery } from 'react-query';
import { useMount } from 'react-use';
import { useTranslation } from 'react-i18next';
import { noop } from 'lodash';
import { PageRef, RestoreContextMethods, RestoreFormContext } from '../../RestoreContext';
import { Backup_States, IBackup } from '../../../..';
import { fetchIncrementalBackup } from '../../../../common/BackupAPI';
import { YBLoadingCircleIcon } from '../../../../../common/indicators';

// eslint-disable-next-line react/display-name
const PrefetchConfigs = React.forwardRef<PageRef>((_, forwardRef) => {
  const [
    {
      backupDetails,
      formData: { generalSettings }
    },
    { moveToNextPage, setBackupDetails }
  ]: RestoreContextMethods = (useContext(RestoreFormContext) as unknown) as RestoreContextMethods;

  const onNext = useCallback(() => {}, []);

  useImperativeHandle(forwardRef, () => ({ onNext, onPrev: noop }), [onNext]);

  const { t } = useTranslation();

  useQuery(
    ['incremental_backups', backupDetails?.commonBackupInfo.baseBackupUUID],
    () => fetchIncrementalBackup(backupDetails!.commonBackupInfo?.baseBackupUUID),
    {
      enabled:
        backupDetails !== null &&
        backupDetails.hasIncrementalBackups &&
        (generalSettings?.incrementalBackupProps.isRestoreEntireBackup ||
          !generalSettings?.incrementalBackupProps.singleKeyspaceRestore), // "restore to the point"
      onSuccess(data) {
        const incrementalBackup = generalSettings?.incrementalBackupProps.incrementalBackupUUID
          ? data.data.find(
              (ic) =>
                ic.backupUUID === generalSettings?.incrementalBackupProps.incrementalBackupUUID
            ) // restore to the point
          : data.data.filter((ic) => ic.state === Backup_States.COMPLETED)[0]; // restore to the lastest succesfull backup
        if (incrementalBackup) {
          setBackupDetails(({
            ...backupDetails,
            commonBackupInfo: incrementalBackup
          } as unknown) as IBackup);
        }
        moveToNextPage();
      }
    }
  );

  useMount(() => {
    // if the backup doesn't have incremental backups, move to next page.
    if (!backupDetails?.hasIncrementalBackups) {
      moveToNextPage();
      return;
    }
    // If single keyspace inside incremental backups is restored, we already have the necessary information for restore.
    // so, set storage location as response list and move to next page.
    if (
      generalSettings?.incrementalBackupProps.singleKeyspaceRestore &&
      generalSettings?.incrementalBackupProps.incrementalBackupUUID
    ) {
      setBackupDetails({
        ...backupDetails,
        commonBackupInfo: {
          ...backupDetails.commonBackupInfo,
          backupUUID: generalSettings?.incrementalBackupProps.incrementalBackupUUID
        }
      });
      moveToNextPage();
    }
  });

  return (
    <>
      <YBLoadingCircleIcon />
      <div style={{ textAlign: 'center' }}>{t('newRestoreModal.prefetch.loadingText')}</div>
    </>
  );
});

export default PrefetchConfigs;
