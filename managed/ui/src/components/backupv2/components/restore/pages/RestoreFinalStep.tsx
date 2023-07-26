/*
 * Created on Tue Jul 04 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { useContext } from 'react';
import { useMount } from 'react-use';
import { useMutation } from 'react-query';
import { toast } from 'react-toastify';
import { find } from 'lodash';
import { Trans, useTranslation } from 'react-i18next';
import { RestoreContextMethods, RestoreFormContext } from '../RestoreContext';
import { restoreBackup, restoreBackupProps } from '../api';
import { Keyspace_Table, RESTORE_ACTION_TYPE } from '../../../common/IBackup';
import { createErrorMessage } from '../../../../../redesign/features/universe/universe-form/utils/helpers';
import { YBLoadingCircleIcon } from '../../../../common/indicators';
import { TableType } from '../../../../../redesign/helpers/dtos';

// this is the final page of the restore modal;
// prepares the payload and triggers the api request
// eslint-disable-next-line react/display-name
const RestoreFinalStep = React.forwardRef(() => {
  const restoreContext = (useContext(RestoreFormContext) as unknown) as RestoreContextMethods;
  const [, , { hideModal }] = restoreContext;
  const { t } = useTranslation();

  const restoreBackupApi = useMutation((backup: restoreBackupProps) => restoreBackup(backup), {
    onSuccess: (resp) => {
      hideModal();
      toast.success(
        <span>
          <Trans
            i18nKey="newRestoreModal.restoreSuccess"
            components={[
              <a href={`/tasks/${resp.data.taskUUID}`} target="_blank" rel="noopener noreferrer">
                here
              </a>
            ]}
          ></Trans>
        </span>
      );
    },
    onError: (resp) => {
      toast.error(createErrorMessage(resp));
      hideModal();
    }
  });

  useMount(() => {
    const payload = preparePayload(restoreContext);
    restoreBackupApi.mutate(payload);
  });

  return (
    <>
      <YBLoadingCircleIcon />
      <div style={{ textAlign: 'center' }}>{t('newRestoreModal.preparingRestore')}</div>
    </>
  );
});

const preparePayload = (restoreContext: RestoreContextMethods): restoreBackupProps => {
  const [
    {
      backupDetails,
      formData: { generalSettings, renamedKeyspaces, selectedTables }
    }
  ] = restoreContext;

  const isKeyspaceRenamed = generalSettings?.renameKeyspace;

  const storageInfoList = backupDetails!.commonBackupInfo.responseList.map(
    (keyspace: Keyspace_Table, index: number) => {
      let keyspacename = keyspace.keyspace;

      if (isKeyspaceRenamed) {
        const origKeyspace = find(renamedKeyspaces.renamedKeyspaces, {
          perBackupLocationKeyspaceTables: { originalKeyspace: keyspace.keyspace }
        });
        if (origKeyspace && origKeyspace.renamedKeyspace !== '') {
          keyspacename = origKeyspace.renamedKeyspace ?? keyspacename;
        }
      }

      const infoList = {
        backupType: backupDetails!.backupType,
        keyspace: keyspacename,
        sse: backupDetails!.commonBackupInfo.sse,
        storageLocation:
          backupDetails?.commonBackupInfo.responseList[index].storageLocation ??
          backupDetails?.commonBackupInfo.responseList[index].defaultLocation
      };

      if (backupDetails?.backupType === TableType.YQL_TABLE_TYPE) {
        infoList['tableNameList'] = backupDetails?.commonBackupInfo.responseList[index].tablesList;
        if (generalSettings?.tableSelectionType === 'SUBSET_OF_TABLES') {
          infoList['tableNameList'] = selectedTables.selectedTables;
          infoList['selectiveTableRestore'] = true;
        }
      }

      return infoList;
    }
  );

  const payload: restoreBackupProps = {
    actionType: RESTORE_ACTION_TYPE.RESTORE,
    kmsConfigUUID: generalSettings?.kmsConfig?.value,
    storageConfigUUID: backupDetails!.commonBackupInfo.storageConfigUUID,
    universeUUID: generalSettings!.targetUniverse!.value,
    backupStorageInfoList: storageInfoList
  };

  if (backupDetails?.category === 'YB_BACKUP_SCRIPT') {
    payload['parallelism'] = generalSettings?.parallelThreads;
  }

  return payload;
};

export default RestoreFinalStep;
