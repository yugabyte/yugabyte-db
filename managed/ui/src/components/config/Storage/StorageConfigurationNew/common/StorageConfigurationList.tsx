/*
 * Created on Thu Jul 21 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC, useState } from 'react';
import { DropdownButton, MenuItem } from 'react-bootstrap';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { Button } from 'react-bootstrap/lib/InputGroup';
import { useMutation } from 'react-query';
import { useSelector } from 'react-redux';
import { toast } from 'react-toastify';

import { getPromiseState } from '../../../../../utils/PromiseUtils';
import { IStorageConfig } from '../../../../backupv2';
import { AssociatedBackups } from '../../../../backupv2/components/AssociatedBackups';
import { AssociatedUniverse } from '../../../../common/associatedUniverse/AssociatedUniverse';
import YBInfoTip from '../../../../common/descriptors/YBInfoTip';
import { FlexContainer, FlexShrink } from '../../../../common/flexbox/YBFlexBox';
import { YBLoading } from '../../../../common/indicators';
import { StorageConfigDeleteModal } from '../../StorageConfigDeleteModal';
import { IStorageProviders } from '../IStorageConfigs';
import { deleteCustomerConfig } from './StorageConfigApi';
import { RbacValidator } from '../../../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../../../redesign/features/rbac/ApiAndUserPermMapping';

interface StorageConfigurationListProps {
  type: IStorageProviders;
  setEditConfigData: (configData: any) => void;
  showStorageConfigCreation: () => void;
  fetchConfigs: () => void;
}

const getBackupLocation = (_: string, row: any) => row.data.BACKUP_LOCATION;
const getBackupStatus = (_: string, row: any) => (row.inUse ? 'Used' : 'Not Used');

export const StorageConfigurationList: FC<StorageConfigurationListProps> = ({
  type,
  setEditConfigData,
  showStorageConfigCreation,
  fetchConfigs
}) => {
  const [showAssociatedBackups, setShowAssociatedBackups] = useState(false);
  const [configData, setConfigData] = useState<any>({});
  const [deleteModalVisible, setDeleteModalVisible] = useState(false);
  const [associatedUniverses, setUniverseDetails] = useState<any>([]);
  const [isUniverseVisible, setIsUniverseVisible] = useState(false);

  const doDeleteStorageConfig = useMutation(
    (configUUID: string) => deleteCustomerConfig(configUUID),
    {
      onSuccess: () => {
        toast.success('storage config deleted successfully!.');
        setDeleteModalVisible(false);
        fetchConfigs();
      }
    }
  );

  const configs:
    | {
      data: IStorageConfig[];
    }
    | undefined = useSelector((state: any) => state.customer.configs);

  if (getPromiseState(configs).isLoading()) {
    return <YBLoading />;
  }

  const backupConfigsByType = configs?.data.filter((t) => t.name === type.toUpperCase()) ?? [];

  const formatConfigActions = (cell: string, row: any) => {
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
                setEditConfigData(row);
              }}
            >
              Edit Configuration
            </MenuItem>
          </RbacValidator>
          <MenuItem
            onClick={(e) => {
              e.stopPropagation();
              setShowAssociatedBackups(true);
              setConfigData({ configUUID, configName });
            }}
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
              onClick={(e) => {
                e.stopPropagation();
                if (!inUse) {
                  setConfigData({ configUUID, configName });
                  setDeleteModalVisible(true);
                }
              }}
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
          <MenuItem
            onClick={() => {
              setConfigData(configName);
              setUniverseDetails([...universeDetails]);
              setIsUniverseVisible(true);
            }}
          >
            Show Universes
          </MenuItem>
        </DropdownButton>
      </>
    );
  };

  return (
    <RbacValidator
      accessRequiredOn={ApiPermissionMap.GET_CUSTOMER_CONFIGS}
    >
      <>
        <h2 className="table-container-title pull-left">Backup List</h2>
        <FlexContainer className="pull-right" direction={'row'}>
          <FlexShrink className="" power={1}>
            <RbacValidator
              accessRequiredOn={ApiPermissionMap.CREATE_CUSTOMER_CONFIG}
              isControl
            >
              <Button bsClass="btn btn-orange btn-config" onClick={() => showStorageConfigCreation()}>
                Create {type.toUpperCase()} Backup
              </Button>
            </RbacValidator>
          </FlexShrink>
        </FlexContainer>
        <BootstrapTable data={backupConfigsByType}>
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
        <StorageConfigDeleteModal
          visible={deleteModalVisible}
          onHide={() => setDeleteModalVisible(false)}
          configName={configData.configName}
          configUUID={configData.configUUID}
          onDelete={() => {
            doDeleteStorageConfig.mutate(configData.configUUID);
          }}
        />
      </>
    </RbacValidator>
  );
};
