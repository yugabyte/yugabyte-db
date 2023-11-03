/*
 * Created on Fri Feb 25 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC, useState } from 'react';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { useMutation } from 'react-query';
import { useSelector } from 'react-redux';
import { toast } from 'react-toastify';
import { IBackup } from '..';
import { YBModalForm } from '../../common/forms';
// import { assignStorageConfig } from '../../backupv2';
import { assignStorageConfig } from '../../backupv2/common/BackupAPI';
import { IStorageConfig } from '../common/IBackup';

import './AssignBackupStorageConfig.scss';

interface BackupStorageConfigProps {
  visible: boolean;
  onHide: () => void;
  backup: IBackup | null;
}

export const AssignBackupStorageConfig: FC<BackupStorageConfigProps> = ({
  visible,
  onHide,
  backup
}) => {
  const configs: IStorageConfig[] = useSelector((state: any) => state.customer.configs.data);

  const [selectedConfig, setSelectedConfig] = useState<IStorageConfig | null>(null);
  const [showErrMsg, setshowErrMsg] = useState(false);
  const doAssignConfig = useMutation(() => assignStorageConfig(backup!, selectedConfig!), {
    onSuccess: () => {
      toast.success(`Storage config ${selectedConfig?.configName} assigned successfully!.`);
      onHide();
    },
    onError: (err: any) => {
      onHide();
      toast.error(err.response.data.error);
    }
  });

  if (!visible || !backup) return null;

  return (
    <YBModalForm
      visible={visible}
      title="Assign storage config to backup"
      size="large"
      onHide={onHide}
      className="backup-modal storage-config"
      onFormSubmit={async (_values: any, { setSubmitting }: { setSubmitting: Function }) => {
        setSubmitting(false);
        if (!selectedConfig) {
          setshowErrMsg(true);
          return;
        }
        doAssignConfig.mutateAsync();
      }}
      submitLabel="Assign"
    >
      <div className="storage-location-path">
        <div>
          <div className="title">Selected backup location:</div>
          {backup.commonBackupInfo.responseList[0].defaultLocation ??
            backup.commonBackupInfo.responseList[0].storageLocation}
        </div>
      </div>
      <div className="help-text">
        <div className="title">Select an existing storage config to assign to this backup.</div>
        <div>
          You can also &nbsp;
          <a
            href="/config/backup/"
            target="_blank"
            rel="noopener noreferrer"
            className="storage-link"
          >
            create
          </a>{' '}
          &nbsp; and assign new storage config.
        </div>
      </div>
      <div className="storage-config-table">
        {showErrMsg && <div className="err-msg">Please select a storage config</div>}
        <BootstrapTable
          data={configs?.filter((c) => c.type === 'STORAGE') ?? []}
          selectRow={{
            mode: 'radio',
            clickToSelect: true,
            onSelect: (row) => setSelectedConfig(row)
          }}
        >
          <TableHeaderColumn dataField="configUUID" isKey={true} hidden={true} />
          <TableHeaderColumn dataField="configName">Configuration Name</TableHeaderColumn>
          <TableHeaderColumn dataField="data" dataFormat={(d) => d.BACKUP_LOCATION}>
            Backup Location
          </TableHeaderColumn>
        </BootstrapTable>
      </div>
    </YBModalForm>
  );
};
