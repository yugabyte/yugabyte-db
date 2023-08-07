/*
 * Created on Thu Mar 24 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import { useQuery } from 'react-query';
import { IBackup } from '../../backupv2';
import { getBackupsList } from '../../backupv2/common/BackupAPI';
import { YBModal } from '../../common/forms/fields';
import { YBLoading } from '../../common/indicators';
import KeyIcon from '../../users/icons/warning_icon';

const HAS_BACKUP_ERR_MSG = (configName: string, configUUID: string) => (
  <>
    <KeyIcon />
    <span>
      <b>Note!</b> There are backups associated with <b>{configName}</b> configuration. To delete a
      configuration you must first delete all its{' '}
      <a
        href={`/backups?storage_config_id=${configUUID}`}
        target="_blank"
        rel="noopener noreferrer"
      >
        associated backups
      </a>
      .
    </span>
  </>
);

interface StorageConfigDeleteModalProps {
  visible: boolean;
  onHide: () => void;
  configName: string;
  configUUID: string;
  onDelete: () => void;
}

export const StorageConfigDeleteModal: FC<StorageConfigDeleteModalProps> = ({
  visible,
  onHide,
  configName,
  configUUID,
  onDelete
}) => {
  const { data: backupsList, isLoading } = useQuery(
    ['associated_backups_del', configName, configUUID],
    () =>
      getBackupsList(
        0,
        1,
        '',
        { startTime: undefined, endTime: undefined, label: undefined },
        [],
        'createTime',
        'DESC',
        undefined,
        undefined,
        configUUID
      ),
    {
      enabled: visible
    }
  );

  if (isLoading) {
    return <YBLoading />;
  }
  const associatedBackups: IBackup[] = backupsList?.data.entities;

  return (
    <YBModal
      name="delete-storage-config"
      title="Delete Configuration"
      onHide={onHide}
      submitLabel="Delete"
      showCancelButton={associatedBackups?.length === 0}
      onFormSubmit={
        associatedBackups?.length !== 0
          ? null
          : () => {
              onDelete();
            }
      }
      visible={visible}
    >
      <div className="storage-config-del-err-msg">
        {associatedBackups?.length > 0 ? (
          HAS_BACKUP_ERR_MSG(configName, configUUID)
        ) : (
          <span>
            Are you sure you want to delete <b>{configName}</b> Storage Configuration?
          </span>
        )}
      </div>
    </YBModal>
  );
};
