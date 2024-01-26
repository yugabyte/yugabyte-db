/*
 * Created on Mon Jun 06 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { useState } from 'react';
import moment from 'moment';
import _ from 'lodash';
import { useSelector } from 'react-redux';
import { DropdownButton, MenuItem, Row } from 'react-bootstrap';
import { RemoteObjSpec, SortOrder, TableHeaderColumn } from 'react-bootstrap-table';
import { useQuery } from 'react-query';
import { YBButton } from '../../common/forms/fields';
import { YBSearchInput } from '../../common/forms/fields/YBSearchInput';
import { YBErrorIndicator, YBLoading } from '../../common/indicators';
import { getPITRConfigs } from '../common/PitrAPI';
import { PointInTimeRecoveryEmpty } from './PointInTimeRecoveryEmpty';
import { PointInTimeRecoveryEnableModal } from './PointInTimeRecoveryEnableModal';
import { YBTable } from '../../common/YBTable';
import { PointInTimeRecoveryModal } from './PointInTimeRecoveryModal';
import { TableTypeLabel } from '../../../redesign/helpers/dtos';
import { PointInTimeRecoveryDisableModal } from './PointInTimeRecoveryDisableModal';
import './PointInTimeRecoveryList.scss';
import { ybFormatDate } from '../../../redesign/helpers/DateUtils';
import { RbacValidator, hasNecessaryPerm } from '../../../redesign/features/rbac/common/RbacValidator';
import { UserPermissionMap } from '../../../redesign/features/rbac/UserPermPathMapping';

const DEFAULT_SORT_COLUMN = 'dbName';
const DEFAULT_SORT_DIRECTION = 'ASC';

export const FormatUnixTimeStampTimeToTimezone = ({ timestamp }: { timestamp: any }) => {
  const currentUserTimezone = useSelector((state: any) => state.customer.currentUser.data.timezone);
  if (!timestamp) return <span>-</span>;
  const formatTime = (currentUserTimezone
    ? (moment.utc(timestamp) as any).tz(currentUserTimezone)
    : moment.utc(timestamp)
  ).format('YYYY/MM/DD h:mmA');
  return <span>{formatTime}</span>;
};

export const PointInTimeRecoveryList = ({ universeUUID }: { universeUUID: string }) => {
  const { data: configs, isLoading, isError } = useQuery(
    ['scheduled_sanpshots', universeUUID],
    () => getPITRConfigs(universeUUID),
    {
      refetchInterval: 1000 * 30
    }
  );

  const [showEnableModal, setShowEnableModal] = useState(false);
  const [recoveryItem, setRecoveryItem] = useState(null);
  const [itemToDisable, setItemToDisable] = useState(null);
  const [sizePerPage, setSizePerPage] = useState(10);
  const [page, setPage] = useState(1);
  const [searchText, setSearchText] = useState('');

  const getActions = (row: any) => {
    return (
      <DropdownButton
        className="actions-btn"
        title="..."
        id={`pitr-actions-dropdown-${row.dbName}-${TableTypeLabel[row.tableType]}`}
        data-testid={`PitrActionBtn-${row.dbName}-${TableTypeLabel[row.tableType]}`}
        noCaret
        pullRight
        onClick={(e: any) => e.stopPropagation()}
      >
        <RbacValidator
          accessRequiredOn={{
            onResource: universeUUID,
            ...UserPermissionMap.restoreBackup
          }}
          isControl
          overrideStyle={{
            display: 'unset'
          }}
        >
          <MenuItem
            onClick={(e: any) => {
              e.stopPropagation();
              row.minRecoverTimeInMillis && setRecoveryItem(_.cloneDeep(row));
            }}
            disabled={!row.minRecoverTimeInMillis}
            data-testid="PitrRecoveryAction"
          >
            Recover to a Point in Time
          </MenuItem>
        </RbacValidator>
        <RbacValidator
          accessRequiredOn={{
            onResource: universeUUID,
            ...UserPermissionMap.editBackup
          }}
          overrideStyle={{
            display: 'unset'
          }}
          isControl
        >
          <MenuItem
            onClick={(e: any) => {
              e.stopPropagation();
              row.minRecoverTimeInMillis && setItemToDisable(_.cloneDeep(row));
            }}
            className="action-danger"
            disabled={!row.minRecoverTimeInMillis}
            data-testid="PitrDisableAction"
          >
            Disable Point-in-time Recovery
          </MenuItem>
        </RbacValidator>
      </DropdownButton>
    );
  };

  if (isLoading) return <YBLoading />;
  if (isError)
    return (
      <Row className="point-in-time-recovery-err">
        <YBErrorIndicator customErrorMessage="Unable to load PITR Configurations" />
      </Row>
    );

  const regex = new RegExp(searchText.replace(/\\/g, '\\\\').toLowerCase() ?? '');
  const pitr_list = configs.filter(
    (config: any) =>
      regex.test(config.dbName.toLowerCase()) ||
      regex.test(TableTypeLabel[config.tableType].toLowerCase())
  );

  return (
    <RbacValidator accessRequiredOn={{
      ...UserPermissionMap.listBackup
    }}
    >
      <Row className="point-in-time-recovery">
        {configs.length === 0 && (
          <PointInTimeRecoveryEmpty onActionButtonClick={() => setShowEnableModal(true)} disabled={!hasNecessaryPerm({ onResource: universeUUID, ...UserPermissionMap.createBackup })} />
        )}
        {configs.length > 0 && (
          <>
            <div className="pitr-actions">
              <YBSearchInput
                placeHolder="Search Database/Keyspace name, API type"
                onValueChanged={(e: React.ChangeEvent<HTMLInputElement>) => {
                  setSearchText(e.target.value);
                }}
              />
              <RbacValidator accessRequiredOn={{
                onResource: universeUUID,
                ...UserPermissionMap.createBackup
              }}
                isControl
              >
                <YBButton
                  btnClass="btn btn-orange backup-empty-button"
                  btnText="Enable Point-In-Time Recovery"
                  onClick={() => setShowEnableModal(true)}
                  id="EnablePitrBtn"
                />
              </RbacValidator>
            </div>
            <div className="info-text">Databases/Keyspaces with Point-In-Time Recovery Enabled</div>
            <YBTable
              data={pitr_list}
              options={{
                sizePerPage,
                onSizePerPageList: setSizePerPage,
                page,
                prePage: 'Prev',
                nextPage: 'Next',
                onPageChange: (page) => setPage(page),
                defaultSortOrder: DEFAULT_SORT_DIRECTION.toLowerCase() as SortOrder,
                defaultSortName: DEFAULT_SORT_COLUMN
              }}
              pagination={true}
              remote={(remoteObj: RemoteObjSpec) => {
                return {
                  ...remoteObj,
                  pagination: true
                };
              }}
              fetchInfo={{ dataTotalSize: pitr_list?.length }}
              hover
            >
              <TableHeaderColumn dataField="backupUUID" isKey={true} hidden={true} />
              <TableHeaderColumn dataField="dbName" dataSort>
                Database/Keyspace Name
              </TableHeaderColumn>
              <TableHeaderColumn
                dataField="tableType"
                dataFormat={(tableType) => TableTypeLabel[tableType]}
                dataSort
              >
                API Type
              </TableHeaderColumn>
              <TableHeaderColumn
                dataField="retentionPeriod"
                dataFormat={(retentionPeriod) => {
                  const retentionDays = retentionPeriod / (24 * 60 * 60);
                  return `${retentionDays} Day${retentionDays > 1 ? 's' : ''}`;
                }}
                dataSort
              >
                Retention Period
              </TableHeaderColumn>
              <TableHeaderColumn
                dataField="minRecoverTimeInMillis"
                dataFormat={(minTime) => {
                  return minTime ? ybFormatDate(minTime) : '';
                }}
                dataSort
              >
                Earliest Recoverable Time
              </TableHeaderColumn>
              <TableHeaderColumn
                dataField="actions"
                dataAlign="right"
                dataFormat={(_: any, row: any) => getActions(row)}
                columnClassName="yb-actions-cell no-border"
                width="10%"
              />
            </YBTable>
          </>
        )}
        <PointInTimeRecoveryEnableModal
          universeUUID={universeUUID}
          visible={showEnableModal}
          onHide={() => setShowEnableModal(false)}
        />
        <PointInTimeRecoveryModal
          onHide={() => setRecoveryItem(null)}
          visible={!!recoveryItem}
          config={recoveryItem}
          universeUUID={universeUUID}
        />

        <PointInTimeRecoveryDisableModal
          onHide={() => setItemToDisable(null)}
          visible={!!itemToDisable}
          config={itemToDisable}
          universeUUID={universeUUID}
        />
      </Row>
    </RbacValidator>
  );
};
