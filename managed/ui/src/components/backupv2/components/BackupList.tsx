/*
 * Created on Thu Feb 10 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import moment from 'moment';
import React, { FC, useMemo, useReducer, useState } from 'react';
import { Col, DropdownButton, MenuItem, Row } from 'react-bootstrap';
import { BootstrapTable, RemoteObjSpec, SortOrder, TableHeaderColumn } from 'react-bootstrap-table';
import { useQuery } from 'react-query';
import { useSelector } from 'react-redux';
import Select, { OptionTypeBase } from 'react-select';
import { Backup_States, getBackupsList, IBackup, TIME_RANGE_STATE } from '..';
import { StatusBadge } from '../../common/badge/StatusBadge';
import { YBButton, YBMultiSelectRedesiged } from '../../common/forms/fields';
import { YBLoading } from '../../common/indicators';
import { BackupDetails } from './BackupDetails';
import {
  BACKUP_STATUS_OPTIONS,
  calculateDuration,
  CALDENDAR_ICON,
  convertArrayToMap,
  DATE_FORMAT,
  ENTITY_NOT_AVAILABLE,
  FormatUnixTimeStampTimeToTimezone
} from '../common/BackupUtils';
import './BackupList.scss';
import { BackupCancelModal, BackupDeleteModal } from './BackupDeleteModal';
import { BackupRestoreModal } from './BackupRestoreModal';
import { YBSearchInput } from '../../common/forms/fields/YBSearchInput';
import { BackupCreateModal } from './BackupCreateModal';
import { useSearchParam } from 'react-use';

const reactWidgets = require('react-widgets');
const momentLocalizer = require('react-widgets-moment');
require('react-widgets/dist/css/react-widgets.css');

const { DateTimePicker } = reactWidgets;
momentLocalizer(moment);

const DEFAULT_SORT_COLUMN = 'createTime';
const DEFAULT_SORT_DIRECTION = 'DESC';

const TIME_RANGE_OPTIONS = [
  {
    value: [1, 'days'],
    label: 'Last 24 hrs'
  },
  {
    value: [3, 'days'],
    label: 'Last 3 days'
  },
  {
    value: [7, 'days'],
    label: 'Last week'
  },
  {
    value: [1, 'month'],
    label: 'Last month'
  },
  {
    value: [3, 'month'],
    label: 'Last 3 months'
  },
  {
    value: [6, 'month'],
    label: 'Last 6 months'
  },
  {
    value: [1, 'year'],
    label: 'Last year'
  },
  {
    value: [0, 'min'],
    label: 'All time'
  },
  {
    value: null,
    label: 'Custom'
  }
];

const DEFAULT_TIME_STATE: TIME_RANGE_STATE = {
  startTime: null,
  endTime: null,
  label: null
};

interface BackupListOptions {
  allowTakingBackup?: boolean;
  universeUUID?: string;
}

export const BackupList: FC<BackupListOptions> = ({ allowTakingBackup, universeUUID }) => {
  const [sizePerPage, setSizePerPage] = useState(10);
  const [page, setPage] = useState(1);
  const [searchText, setSearchText] = useState('');
  const [customStartTime, setCustomStartTime] = useState<Date | undefined>();
  const [customEndTime, setCustomEndTime] = useState<Date | undefined>();
  const [sortDirection, setSortDirection] = useState(DEFAULT_SORT_DIRECTION);

  const [showDeleteModal, setShowDeleteModal] = useState(false);
  const [showRestoreModal, setShowRestoreModal] = useState(false);
  const [showBackupCreateModal, setShowBackupCreateModal] = useState(false);
  const [selectedBackups, setSelectedBackups] = useState<IBackup[]>([]);
  const [status, setStatus] = useState<any[]>([]);

  const timeReducer = (_state: TIME_RANGE_STATE, action: OptionTypeBase) => {
    if (action.label === 'Custom') {
      return { startTime: customStartTime, endTime: customEndTime, label: action.label };
    }
    if (action.label === 'All time') {
      return { startTime: null, endTime: null, label: action.label };
    }

    return {
      label: action.label,
      startTime: moment().subtract(action.value[0], action.value[1]),
      endTime: new Date()
    };
  };

  const storage_config_uuid = useSearchParam('storage_config_id');

  const [timeRange, dispatchTimeRange] = useReducer(timeReducer, DEFAULT_TIME_STATE);

  const { data: backupsList, isLoading } = useQuery(
    [
      'backups',
      (page - 1) * sizePerPage,
      sizePerPage,
      searchText,
      timeRange,
      status,
      DEFAULT_SORT_COLUMN,
      sortDirection,
      universeUUID,
      storage_config_uuid
    ],
    () =>
      getBackupsList(
        (page - 1) * sizePerPage,
        sizePerPage,
        searchText,
        timeRange,
        status,
        DEFAULT_SORT_COLUMN,
        sortDirection,
        universeUUID,
        storage_config_uuid
      ),
    {
      refetchInterval: 1000 * 20
    }
  );

  const [showDetails, setShowDetails] = useState<IBackup | null>(null);
  const storageConfigs = useSelector((reduxState: any) => reduxState.customer.configs);
  const [restoreDetails, setRestoreDetails] = useState<IBackup | null>(null);
  const [cancelBackupDetails, setCancelBackupDetails] = useState<IBackup | null>(null);

  const storageConfigsMap = useMemo(
    () => convertArrayToMap(storageConfigs?.data ?? [], 'configUUID', 'configName'),
    [storageConfigs]
  );

  const getActions = (row: IBackup) => {
    if (row.state === Backup_States.DELETED) {
      return '';
    }
    if (row.state === Backup_States.IN_PROGRESS) {
      return (
        <YBButton
          onClick={(e: React.MouseEvent<HTMLButtonElement>) => {
            e.stopPropagation();
            setCancelBackupDetails(row);
          }}
          btnClass="btn btn-default backup-cancel"
          btnText="Cancel Backup"
        />
      );
    }
    return (
      <DropdownButton
        className="actions-btn"
        title="..."
        id="backup-actions-dropdown"
        noCaret
        pullRight
        onClick={(e) => e.stopPropagation()}
      >
        <MenuItem
          disabled={row.state !== Backup_States.COMPLETED}
          onClick={(e) => {
            e.stopPropagation();
            if (row.state !== Backup_States.COMPLETED) {
              return;
            }
            setRestoreDetails(row);
            setShowRestoreModal(true);
          }}
        >
          Restore Entire Backup
        </MenuItem>
        <MenuItem
          onClick={(e) => {
            e.stopPropagation();
            setSelectedBackups([row]);
            setShowDeleteModal(true);
          }}
          className="action-danger"
        >
          Delete Backup
        </MenuItem>
      </DropdownButton>
    );
  };

  const backups: IBackup[] = backupsList?.data.entities;
  return (
    <Row className="backup-v2">
      <Row className="backup-actions">
        <Col lg={5} className="no-padding">
          <Row>
            <Col lg={6} className="no-padding">
              <YBSearchInput
                placeHolder="Search universe name, Storage Config, Database/Keyspace name"
                onEnterPressed={(val: string) => setSearchText(val)}
              />
            </Col>
            <Col lg={4}>
              <YBMultiSelectRedesiged
                className="backup-status-filter"
                name="statuses"
                placeholder=""
                isMulti={false}
                options={BACKUP_STATUS_OPTIONS}
                onChange={(value: any) => {
                  setStatus(value ? [value] : []);
                }}
              />
            </Col>
          </Row>
        </Col>
        <Col lg={7} className="actions-delete-filters no-padding">
          <YBButton
            btnText="Delete"
            btnIcon="fa fa-trash-o"
            onClick={() => setShowDeleteModal(true)}
            disabled={selectedBackups.length === 0}
          />
          {timeRange.label === 'Custom' && (
            <div className="custom-date-picker">
              <DateTimePicker
                placeholder="Pick a start time"
                step={10}
                formats={DATE_FORMAT}
                onChange={(time: Date) => {
                  setCustomStartTime(time);
                  dispatchTimeRange({
                    label: 'Custom'
                  });
                }}
              />
              <span>-</span>
              <DateTimePicker
                placeholder="Pick a end time"
                step={10}
                formats={DATE_FORMAT}
                onChange={(time: Date) => {
                  setCustomEndTime(time);
                  dispatchTimeRange({
                    label: 'Custom'
                  });
                }}
              />
            </div>
          )}

          <Select
            className="time-range"
            options={TIME_RANGE_OPTIONS}
            onChange={(value) => {
              dispatchTimeRange({
                ...value
              });
            }}
            styles={{
              input: (styles) => {
                return { ...styles, ...CALDENDAR_ICON() };
              },
              placeholder: (styles) => ({ ...styles, ...CALDENDAR_ICON() }),
              singleValue: (styles) => ({ ...styles, ...CALDENDAR_ICON() }),
              menu: (styles) => ({
                ...styles,
                zIndex: 10
              })
            }}
          ></Select>
          {allowTakingBackup && (
            <YBButton
              btnText="Backup now"
              onClick={() => {
                setShowBackupCreateModal(true);
              }}
              btnClass="btn btn-orange backup-now-button"
              btnIcon="fa fa-upload"
            />
          )}
        </Col>
      </Row>
      <Row className="backup-list-table">
        {isLoading && <YBLoading />}
        <BootstrapTable
          data={backups}
          options={{
            sizePerPage,
            onSizePerPageList: setSizePerPage,
            page,
            prePage: 'Prev',
            nextPage: 'Next',
            onRowClick: (row) => setShowDetails(row),
            onPageChange: (page) => setPage(page),
            defaultSortOrder: DEFAULT_SORT_DIRECTION.toLowerCase() as SortOrder,
            defaultSortName: DEFAULT_SORT_COLUMN,
            onSortChange: (_: any, SortOrder: SortOrder) =>
              setSortDirection(SortOrder.toUpperCase())
          }}
          selectRow={{
            mode: 'checkbox',
            selected: selectedBackups.map((b) => b.backupUUID),
            onSelect: (row, isSelected) => {
              if (isSelected) {
                setSelectedBackups([...selectedBackups, row]);
              } else {
                setSelectedBackups(selectedBackups.filter((b) => b.backupUUID !== row.backupUUID));
              }
            },
            onSelectAll: (isSelected, row) => {
              isSelected ? setSelectedBackups(row) : setSelectedBackups([]);
              return true;
            }
          }}
          trClassName="table-row"
          tableHeaderClass="backup-list-header"
          pagination={true}
          remote={(remoteObj: RemoteObjSpec) => {
            return {
              ...remoteObj,
              pagination: true
            };
          }}
          fetchInfo={{ dataTotalSize: backupsList?.data.totalCount }}
          hover
        >
          <TableHeaderColumn dataField="backupUUID" isKey={true} hidden={true} />
          <TableHeaderColumn
            dataField="universeUUID"
            dataFormat={(_name, row: IBackup) =>
              row.universeName ? row.universeName : ENTITY_NOT_AVAILABLE
            }
            width="20%"
          >
            Source Universe Name
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="onDemand"
            dataFormat={(onDemand) => (onDemand ? 'On Demand' : 'Scheduled')}
            width="10%"
          >
            Backup Type
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="createTime"
            dataFormat={(time) => <FormatUnixTimeStampTimeToTimezone timestamp={time} />}
            width="20%"
            dataSort
          >
            Created At
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="expiryTime"
            dataFormat={(time) =>
              time ? <FormatUnixTimeStampTimeToTimezone timestamp={time} /> : "Won't Expire"
            }
            width="20%"
          >
            Expiration
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="duration"
            dataFormat={(_, row) => {
              return calculateDuration(row.createTime, row.updateTime);
            }}
            width="20%"
          >
            Duration
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="storageConfigUUID"
            dataFormat={(name) => storageConfigsMap[name] ?? ENTITY_NOT_AVAILABLE}
            width="20%"
          >
            Storage Config
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="state"
            dataFormat={(state) => {
              return (
                <StatusBadge
                  statusType={state}
                  customLabel={state === Backup_States.STOPPED ? 'Cancelled' : ''}
                />
              );
            }}
            width="15%"
          >
            Status
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="actions"
            dataAlign="right"
            dataFormat={(_, row) => getActions(row)}
            columnClassName="yb-actions-cell no-border"
            width="10%"
          />
        </BootstrapTable>
      </Row>
      <BackupDetails
        backup_details={showDetails}
        onHide={() => setShowDetails(null)}
        storageConfigName={showDetails ? storageConfigsMap?.[showDetails?.storageConfigUUID] : '-'}
        onDelete={() => {
          setSelectedBackups([showDetails] as IBackup[]);
          setShowDeleteModal(true);
        }}
        onRestore={(customDetails?: IBackup) => {
          setRestoreDetails(customDetails ?? showDetails);
          setShowRestoreModal(true);
        }}
        storageConfigs={storageConfigs}
      />
      <BackupDeleteModal
        backupsList={selectedBackups}
        visible={showDeleteModal}
        onHide={() => setShowDeleteModal(false)}
      />
      <BackupRestoreModal
        backup_details={restoreDetails}
        visible={showRestoreModal}
        onHide={() => {
          setShowRestoreModal(false);
        }}
      />
      <BackupCancelModal
        visible={cancelBackupDetails !== null}
        onHide={() => setCancelBackupDetails(null)}
        backup={cancelBackupDetails}
      />
      <BackupCreateModal
        visible={showBackupCreateModal}
        onHide={() => {
          setShowBackupCreateModal(false);
        }}
        currentUniverseUUID={universeUUID}
      />
    </Row>
  );
};
