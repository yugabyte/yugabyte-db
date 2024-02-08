// Copyright (c) YugaByte, Inc.
//
// Author: Nishant Sharma(nishant.sharma@hashedin.com)
//
// This file will hold a common list view for the different kind
// of storage configuration.

import { useState } from 'react';
import { Button, DropdownButton, MenuItem } from 'react-bootstrap';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { AssociatedBackups } from '../../backupv2/components/AssociatedBackups';
import { AssociatedUniverse } from '../../common/associatedUniverse/AssociatedUniverse';
import YBInfoTip from '../../common/descriptors/YBInfoTip';
import { FlexContainer, FlexShrink } from '../../common/flexbox/YBFlexBox';

import { YBPanelItem } from '../../panels';

import { StorageConfigDeleteModal } from './StorageConfigDeleteModal';
import { RbacValidator, hasNecessaryPerm } from '../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../redesign/features/rbac/ApiAndUserPermMapping';


/**
 * This method is used to return the current in-use status
 * of the backup storage config.
 *
 * @param {string} cell Not in-use.
 * @param {object} row Respective row details.
 * @returns "Used" || "Not Used".
 */
const getBackupStatus = (cell, row) => (row.inUse ? 'Used' : 'Not Used');

/**
 * This method is used to return the backup location of
 * respective backup config.
 *
 * @param {string} cell Not in-use.
 * @param {object} row Respective row details.
 * @returns Backup storage location.
 */
const getBackupLocation = (cell, row) => row.data.BACKUP_LOCATION;

/**
 * This is the header for YB Panel Item.
 *
 * @param {string} currTab Active tab.
 * @param {prop} onCreateBackup Click event.
 */
const header = (currTab, onCreateBackup) => (
  <>
    <h2 className="table-container-title pull-left">Backup List</h2>
    <FlexContainer className="pull-right">
      <FlexShrink>
        <RbacValidator
          accessRequiredOn={ApiPermissionMap.CREATE_CUSTOMER_CONFIG}
          isControl
        >
          <Button bsClass="btn btn-orange btn-config" onClick={onCreateBackup}>
            Create {currTab} Backup
          </Button>
        </RbacValidator>
      </FlexShrink>
    </FlexContainer>
  </>
);

export const BackupList = (props) => {
  const [configData, setConfigData] = useState({});
  const [associatedUniverses, setUniverseDetails] = useState([]);
  const [isUniverseVisible, setIsUniverseVisible] = useState(false);
  const [showAssociatedBackups, setShowAssociatedBackups] = useState(false);

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

  // This method will handle all the required actions for
  // the particular row.
  const formatConfigActions = (cell, row) => {
    const { configName, configUUID, inUse, universeDetails } = row;
    return (
      <>
        <DropdownButton
          className="backup-config-action-but btn btn-default"
          title="Actions"
          id="bg-nested-dropdown"
          pullRight
        >
          <RbacValidator
            accessRequiredOn={ApiPermissionMap.EDIT_CUSTOMER_CONFIG}
            isControl
            overrideStyle={{ display: 'block' }}
          >
            <MenuItem
              onClick={() => {
                onEditConfig(row);
              }}
              data-testid={`${currTab}-BackupList-EditConfiguration`}
            >
              Edit Configuration
            </MenuItem>
          </RbacValidator>
          <MenuItem
            onClick={(e) => {
              if (!hasNecessaryPerm(ApiPermissionMap.GET_BACKUP)) {
                return;
              }
              e.stopPropagation();
              setShowAssociatedBackups(true);
              setConfigData({ configUUID, configName });
            }}
            disabled={!hasNecessaryPerm(ApiPermissionMap.GET_BACKUP)}
            data-testid={`${currTab}-BackupList-ShowAssociatedBackups`}
          >
            Show associated backups
          </MenuItem>
          <RbacValidator
            accessRequiredOn={ApiPermissionMap.DELETE_CUSTOMER_CONFIG}
            isControl
            overrideStyle={{ display: 'block' }}
          >
            <MenuItem
              disabled={inUse}
              onClick={() => {
                if (!inUse) {
                  setConfigData(configUUID);
                  showDeleteStorageConfig(configName);
                }
              }}
              data-testid={`${currTab}-BackupList-DeleteConfiguration`}
            >
              {!inUse && <>Delete Configuration</>}

              {inUse && (
                <YBInfoTip
                  content="Storage configuration is in use and cannot be deleted until associated resources are removed."
                  placement="top"
                >
                  <span className="disable-delete">Delete Configuration</span>
                </YBInfoTip>
              )}
            </MenuItem>
          </RbacValidator>
          <StorageConfigDeleteModal
            visible={visibleModal === 'delete' + configName + 'StorageConfig'}
            onHide={hideDeleteStorageConfig}
            configName={configName}
            configUUID={configData}
            onDelete={() => {
              deleteStorageConfig(configData);
            }}
          />
          <MenuItem
            onClick={() => {
              setConfigData(configName);
              setUniverseDetails([...universeDetails]);
              setIsUniverseVisible(true);
            }}
            data-testid={`${currTab}-BackupList-ShowUniverses`}
          >
            Show Universes
          </MenuItem>
        </DropdownButton>
      </>
    );
  };

  return (
    <YBPanelItem
      header={header(currTab, onCreateBackup)}
      body={
        <>
          <BootstrapTable className="backup-list-table middle-aligned-table" data={data}>
            <TableHeaderColumn dataField="configUUID" isKey={true} hidden={true} />
            <TableHeaderColumn
              dataField="configName"
              columnClassName="no-border name-column"
              className="no-border"
              width="30%"
            >
              Configuration Name
            </TableHeaderColumn>
            <TableHeaderColumn
              dataField="backupLocation"
              dataFormat={getBackupLocation}
              columnClassName="no-border name-column"
              className="no-border"
              width="30%"
            >
              Backup Location
            </TableHeaderColumn>
            <TableHeaderColumn
              dataField="status"
              dataFormat={getBackupStatus}
              columnClassName="no-border name-column"
              className="no-border"
              width="10%"
            >
              Status
            </TableHeaderColumn>
            <TableHeaderColumn
              dataField="configActions"
              dataFormat={(cell, row) => formatConfigActions(cell, row)}
              columnClassName="yb-actions-cell"
              className="yb-actions-cell"
              width="10%"
              dataAlign="center"
            >
              Actions
            </TableHeaderColumn>
          </BootstrapTable>
          <AssociatedUniverse
            visible={isUniverseVisible}
            onHide={() => setIsUniverseVisible(false)}
            associatedUniverses={associatedUniverses}
            title={`Backup Configuration ${configData}`}
          />
          <AssociatedBackups
            visible={showAssociatedBackups}
            onHide={() => setShowAssociatedBackups(false)}
            storageConfigData={configData}
          />
        </>
      }
      noBackground
    />
  );
};
