// Copyright (c) YugaByte, Inc.
// 
// Author: Nishant Sharma(nishant.sharma@hashedin.com)

import React from 'react'
import { Button, DropdownButton, MenuItem } from 'react-bootstrap'
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table'
import { FlexContainer, FlexShrink } from '../../common/flexbox/YBFlexBox'
import { YBPanelItem } from '../../panels'

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

// This method will return all the necessary actions
// for the give list.
const foramtConfigActions = (cell, row) => {
  return (
    <DropdownButton
      className="btn btn-default"
      title="Actions"
      id="bg-nested-dropdown"
      pullRight
    >
      <MenuItem>
        <i className="fa fa-info-circle"></i> Delete Configuration
      </MenuItem>
      <MenuItem>
        <i className="fa fa-download"></i> Show Universes
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
  const currTab = props.activeTab.toUpperCase();

  return (
    <div>
      <YBPanelItem
        header={header(currTab, props.onCreateBackup)}
        body={
          <>
            <BootstrapTable
              className="backup-list-table middle-aligned-table"
              data={props.data}
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
                dataFormat={foramtConfigActions}
                columnClassName="yb-actions-cell"
                width="120px"
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