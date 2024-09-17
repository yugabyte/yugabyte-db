/*
 * Created on Fri Aug 30 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { forwardRef, useContext, useImperativeHandle } from 'react';
import { Trans, useTranslation } from 'react-i18next';
import { useMount } from 'react-use';
import { useMutation } from 'react-query';
import { find } from 'lodash';
import { useFormContext } from 'react-hook-form';
import { toast } from 'react-toastify';

import { YBLoadingCircleIcon } from '../../../../../../components/common/indicators';

import { restoreBackup, RestoreV2BackupProps } from '../../api/api';
import { createErrorMessage } from '../../../../../../utils/ObjectUtils';
import { isPITREnabledInBackup } from '../../RestoreUtils';

import { RESTORE_ACTION_TYPE } from '../../../../../../components/backupv2';
import { TableType } from '../../../../../helpers/dtos';
import { RestoreFormModel, TimeToRestoreType } from '../../models/RestoreFormModel';
import {
  Page,
  PageRef,
  RestoreContext,
  RestoreContextMethods,
  RestoreFormContext
} from '../../models/RestoreContext';

const RestoreFinal = forwardRef<PageRef>((_, forwardRef) => {
  const [restoreContext, { moveToPage, setDisableSubmit }, { hideModal }] = (useContext(
    RestoreFormContext
  ) as unknown) as RestoreContextMethods;

  const { getValues } = useFormContext<RestoreFormModel>();

  const { t } = useTranslation('translation', {
    keyPrefix: 'backup.restore'
  });

  const doRestoreBackup = useMutation((backup: RestoreV2BackupProps) => restoreBackup(backup), {
    onSuccess: (resp) => {
      hideModal();
      toast.success(
        <span>
          <Trans
            i18nKey="restoreStarted"
            t={t}
            components={[
              <a href={`/tasks/${resp.data.taskUUID}`} target="_blank" rel="noopener noreferrer">
                {t('viewDetails')}
              </a>
            ]}
          ></Trans>
        </span>
      );
      hideModal();
    },
    onError: (resp) => {
      toast.error(createErrorMessage(resp));
      hideModal();
    }
  });

  useMount(() => {
    setDisableSubmit(true);

    const payload = preparePayload(restoreContext, getValues());
    payload && doRestoreBackup.mutate(payload);
  });

  useImperativeHandle(forwardRef, () => ({
    onPrev: () => {
      moveToPage(Page.TARGET);
    },
    onNext: () => {}
  }));

  return (
    <>
      <YBLoadingCircleIcon />
      <div style={{ textAlign: 'center' }}>{t('preparingRestore')}</div>
    </>
  );
});

const preparePayload = (
  restoreContext: RestoreContext,
  formValues: RestoreFormModel
): RestoreV2BackupProps | null => {
  const { backupDetails } = restoreContext;

  const {
    target,
    currentCommonBackupInfo,
    renamedKeyspace,
    keyspacesToRestore,
    pitrMillis,
    source
  } = formValues;

  const isKeyspaceRenamed = target.renameKeyspace;

  if (!currentCommonBackupInfo) return null;

  const storageInfoList =
    keyspacesToRestore?.keyspaceTables.map((keyspace, ind) => {
      // iterate userSelected keyspaces
      const keyspaceinfo = find(currentCommonBackupInfo.responseList, {
        keyspace: keyspace.keyspace
      });

      let keyspacename = keyspace.keyspace;

      if (isKeyspaceRenamed) {
        //if keyspace is renamed
        const rKeyspace = renamedKeyspace[ind];
        if (rKeyspace?.renamedKeyspace !== '') {
          keyspacename = rKeyspace.renamedKeyspace ?? keyspaceinfo?.keyspace;
        }
      }

      const infoList = {
        backupType: backupDetails!.backupType,
        keyspace: keyspacename,
        sse: currentCommonBackupInfo.sse,
        storageLocation: keyspaceinfo?.storageLocation ?? keyspaceinfo?.defaultLocation,
        useTablespaces: target.useTablespaces
      } as any;

      if (backupDetails?.backupType === TableType.YQL_TABLE_TYPE) {
        infoList['tableNameList'] = keyspace.tableNames;
      }
      return infoList;
    }) ?? [];

  const payload: RestoreV2BackupProps = {
    actionType: RESTORE_ACTION_TYPE.RESTORE,
    kmsConfigUUID: target.kmsConfig?.value,
    storageConfigUUID: currentCommonBackupInfo.storageConfigUUID,
    universeUUID: target.targetUniverse!.value,
    backupStorageInfoList: storageInfoList
  };

  // if pitr is enabled and selected
  if (
    isPITREnabledInBackup(currentCommonBackupInfo) &&
    source.timeToRestoreType === TimeToRestoreType.EARLIER_POINT_IN_TIME &&
    pitrMillis !== 0
  ) {
    payload['restoreToPointInTimeMillis'] = pitrMillis;
  }

  // if ybc controller is not enabled, then add parallelism
  if (backupDetails?.category === 'YB_BACKUP_SCRIPT') {
    payload['parallelism'] = target.parallelThreads;
  }

  return payload;
};

RestoreFinal.displayName = 'RestoreFinal';
export default RestoreFinal;
