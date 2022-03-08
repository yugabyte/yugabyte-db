/*
 * Created on Fri Feb 25 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { FC, useState } from 'react';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { useSelector } from 'react-redux';
import { IBackup } from '.';
import { YBModalForm } from '../common/forms';

import './BackupStorageConfig.scss';

interface BackupStorageConfigProps {
  visible: boolean;
  onHide: () => void;
  backup?: IBackup;
  onSubmit: Function;
}

export const BackupStorageConfig: FC<BackupStorageConfigProps> = ({
  visible,
  onHide,
  onSubmit
}) => {
  const configs = useSelector((state: any) => state.customer.configs.data);
  const [selectedConfig, setSelectedConfig] = useState(null);
  return (
    <YBModalForm
      visible={visible}
      title="Assign storage config to backup"
      size="large"
      onHide={onHide}
      className="backup-modal storage-config"
      onFormSubmit={async (_values: any, { setSubmitting }: { setSubmitting: Function }) => {
        setSubmitting(false);
        onSubmit(selectedConfig);
        onHide();
      }}
      submitLabel="Assign"
    >
      <div className="storage-location">
        <div>
          <div className="title">Selected backup location:</div>
        </div>
      </div>
      <div className="help-text">
        <div className="title">Select an existing storage config to assign to this backup.</div>
        <div>
          You can also <span className="storage-link">create</span> and assign new storage config.
        </div>
      </div>
      <div className="storage-config-table">
        <BootstrapTable
          data={configs ?? []}
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
