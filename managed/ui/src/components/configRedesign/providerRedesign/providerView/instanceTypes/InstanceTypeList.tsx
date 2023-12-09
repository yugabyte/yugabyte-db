/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import clsx from 'clsx';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { Dropdown, MenuItem } from 'react-bootstrap';

import ellipsisIcon from '../../../../common/media/more.svg';
import { EmptyListPlaceholder } from '../../EmptyListPlaceholder';
import { VolumeList } from './VolumeList';
import { YBLabelWithIcon } from '../../../../common/descriptors';

import { InstanceType, InstanceTypeDetails } from '../../../../../redesign/helpers/dtos';

import styles from './InstanceTypeList.module.scss';

interface InstanceTypeListProps {
  instanceTypes: InstanceType[];
  showAddInstanceTypeFormModal: () => void;
  showDeleteInstanceTypeModal: (instanceTypeField: InstanceType) => void;

  isDisabled?: boolean;
  isError?: boolean;
}

export const InstanceTypeList = ({
  isDisabled,
  instanceTypes,
  isError,
  showAddInstanceTypeFormModal,
  showDeleteInstanceTypeModal
}: InstanceTypeListProps) => {
  const formatInstanceActions = (_: unknown, row: InstanceType) => (
    <div className={styles.buttonContainer} onClick={(event) => event.stopPropagation()}>
      <Dropdown id="instance-type-table-actions-dropdown" pullRight>
        <Dropdown.Toggle noCaret>
          <img src={ellipsisIcon} alt="more" className="ellipsis-icon" />
        </Dropdown.Toggle>
        <Dropdown.Menu>
          <MenuItem
            eventKey="2"
            onClick={() => showDeleteInstanceTypeModal(row)}
            disabled={isDisabled}
          >
            <YBLabelWithIcon icon="fa fa-trash">Delete Instance Type</YBLabelWithIcon>
          </MenuItem>
        </Dropdown.Menu>
      </Dropdown>
    </div>
  );

  const formatVolumes = (instanceTypeDetails: InstanceTypeDetails) => (
    <span>{instanceTypeDetails.volumeDetailsList.length}</span>
  );

  return instanceTypes.length === 0 && !isDisabled ? (
    <EmptyListPlaceholder
      actionButtonText={`Add Instance Type`}
      descriptionText="Add instance types to this provider"
      onActionButtonClick={showAddInstanceTypeFormModal}
      className={clsx(isError && styles.emptyListError)}
      dataTestIdPrefix="InstanceTypeEmptyList"
    />
  ) : (
    <div className={styles.bootstrapTableContainer}>
      <BootstrapTable
        tableContainerClass={styles.bootstrapTable}
        data={instanceTypes}
        expandableRow={(row: InstanceType) => row.instanceTypeDetails.volumeDetailsList.length > 0}
        expandComponent={(row: InstanceType) => (
          <VolumeList volumes={row.instanceTypeDetails.volumeDetailsList} />
        )}
      >
        <TableHeaderColumn dataField="instanceTypeCode" isKey={true} dataSort={true}>
          Name
        </TableHeaderColumn>
        <TableHeaderColumn dataField="numCores" dataSort={true}>
          Cores
        </TableHeaderColumn>
        <TableHeaderColumn dataField="memSizeGB" dataSort={true}>
          Memory Size (GB)
        </TableHeaderColumn>
        <TableHeaderColumn
          dataField="instanceTypeDetails"
          dataSort={true}
          dataFormat={formatVolumes}
        >
          Volumes
        </TableHeaderColumn>
        <TableHeaderColumn
          columnClassName={styles.actionsColumn}
          dataFormat={formatInstanceActions}
          width="50"
        />
      </BootstrapTable>
    </div>
  );
};
