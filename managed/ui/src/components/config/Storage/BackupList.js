// Copyright (c) YugaByte, Inc.
// 
// Author: Nishant Sharma(nishant.sharma@hashedin.com)

import React from 'react';
import { Button, DropdownButton, MenuItem } from 'react-bootstrap';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import YBInfoTip from '../../common/descriptors/YBInfoTip';
import { FlexContainer, FlexShrink } from '../../common/flexbox/YBFlexBox';
import { YBConfirmModal } from '../../modals';
import { YBPanelItem } from '../../panels';

// Data format for configuration name column.
const getBackupConfigName = (cell, row) => {
  return row.data.CONFIGURATION_NAME;
};

// Data format for status column.
const getBackupStatus = (cell, row) => {
  return row.inUse ? "Used" : "Not Used";
};

// Data format for backup location column.
const getBackupLocation = (cell, row) => {
  return row.data.BACKUP_LOCATION;
};

// This method will help us to delete the respective backup config
// if it's not in use.
const handleDeleteBackupConfig = (configUUID) => {
  console.log(configUUID, '******************configUUID');
  // return true;
};

// This method will return all the necessary actions
// for the give list.
const foramtConfigActions = (cell, row, onEdit, deleteStorageConfig, showDeleteStorageConfig, visibleModal, hideDeleteStorageConfig) => {
  const {
    configUUID,
    data,
    inUse,
    name
  } = row;
  const configName = data.CONFIGURATION_NAME;

  return (
    <DropdownButton
      className="backup-config-actions btn btn-default"
      title="Actions"
      id="bg-nested-dropdown"
      pullRight
    >
      <MenuItem onClick={() => onEdit(row)}>
        <i className="fa fa-pencil"></i> Edit Configuration
      </MenuItem>
      <MenuItem
        onClick={() => deleteStorageConfig(configUUID)}//{() => showDeleteStorageConfig(name)}
        disabled={inUse}>
        {!inUse &&
          <><i className="fa fa-trash"></i> Delete Configuration</>
        }

        {inUse &&
          <YBInfoTip content="Storage configuration is in use and cannot be deleted until associated resources are removed."
            placement="top"
          >
            <span className="disable-delete">
              <i className="fa fa-ban"></i> Delete Configuration
            </span>
          </YBInfoTip>
        }
      </MenuItem>

      {/* {!inUse &&
        <YBConfirmModal
          name="delete-storage-config"
          title='Confirm Delete'
          onConfirm={() => deleteStorageConfig(row)}
          currentModal={'delete' + name + 'StorageConfig'}
          visibleModal={visibleModal}
          hideConfirmModal={hideDeleteStorageConfig}
        >
          Are you sure you want to delete {name} Storage Configuration?
        </YBConfirmModal>
      } */}

      <MenuItem>
        <i className="fa fa-eye"></i> Show Universes
      </MenuItem>
    </DropdownButton>
  );
};

const header = (currTab, onCreateBackup) => (
  <>
    <h2 className="table-container-title pull-left">Backup List</h2>
    <FlexContainer className="pull-right">
      <FlexShrink>
        <Button
          bsClass="btn btn-orange btn-config"
          onClick={onCreateBackup}
        >
          Create {currTab} Backup
        </Button>
      </FlexShrink>
    </FlexContainer>
  </>
);

function BackupList(props) {
  const {
    activeTab,
    data,
    deleteStorageConfig,
    hideDeleteStorageConfig,
    onCreateBackup,
    onEditConfig,
    showDeleteStorageConfig,
    visibleModal
  } = props;
  const currTab = activeTab.toUpperCase();
  const onEdit = onEditConfig;

  return (
    <div>
      <YBPanelItem
        header={header(currTab, onCreateBackup)}
        body={
          <>
            <BootstrapTable
              className="backup-list-table middle-aligned-table"
              data={data}
            >
              <TableHeaderColumn
                dataField="configUUID"
                isKey={true}
                hidden={true}
              />
              <TableHeaderColumn
                dataField="configurationName"
                dataFormat={getBackupConfigName}
                columnClassName="no-border name-column"
                className="no-border"
              >
                Configuration Name
                </TableHeaderColumn>
              <TableHeaderColumn
                dataField="status"
                dataFormat={getBackupStatus}
                columnClassName="no-border name-column"
                className="no-border"
              >
                Status
                </TableHeaderColumn>
              <TableHeaderColumn
                dataField="backupLocation"
                dataFormat={getBackupLocation}
                columnClassName="no-border name-column"
                className="no-border"
              >
                Backup Location
              </TableHeaderColumn>
              <TableHeaderColumn
                dataField="configActions"
                dataFormat={(cell, row) => foramtConfigActions(cell, row, onEdit, deleteStorageConfig, showDeleteStorageConfig, visibleModal, hideDeleteStorageConfig)}
                columnClassName="yb-actions-cell"
                className="yb-actions-cell"
              >
                Actions
              </TableHeaderColumn>
            </BootstrapTable>
          </>
        }
        noBackground
      />
    </div>
  )
}

export { BackupList }