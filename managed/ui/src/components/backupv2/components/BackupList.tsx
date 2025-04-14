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
import { DropdownButton, MenuItem, Row } from 'react-bootstrap';
import { RemoteObjSpec, SortOrder, TableHeaderColumn } from 'react-bootstrap-table';
import { useQuery } from 'react-query';
import { useSelector } from 'react-redux';
import Select, { OptionTypeBase } from 'react-select';
import clsx from 'clsx';
import { Backup_States, IBackup, IStorageConfig, TIME_RANGE_STATE } from '..';
import { getBackupsList } from '../common/BackupAPI';
import { StatusBadge } from '../../common/badge/StatusBadge';
import { YBButton, YBMultiSelectRedesiged } from '../../common/forms/fields';
import { YBLoading } from '../../common/indicators';
import { Box, Typography, makeStyles } from '@material-ui/core';
import { YBTooltip } from '../../../redesign/components';
import { BackupDetails, IncrementalBackupProps } from './BackupDetails';
import {
  BACKUP_REFETCH_INTERVAL,
  BACKUP_STATUS_OPTIONS,
  CALDENDAR_ICON,
  convertArrayToMap,
  convertBackupToFormValues,
  DATE_FORMAT,
  ENTITY_NOT_AVAILABLE,
  isBackupPITREnabled
} from '../common/BackupUtils';
import { BackupCancelModal, BackupDeleteModal } from './BackupDeleteModal';
import { BackupRestoreModal } from './BackupRestoreModal';
import { default as BackupRestoreModalWithPITR } from '../../../redesign/features/backup/restore/BackupRestoreModal';
import BackupRestoreNewModal from './restore/BackupRestoreNewModal';
import { YBSearchInput } from '../../common/forms/fields/YBSearchInput';
import { BackupCreateModal } from './BackupCreateModal';
import { useSearchParam } from 'react-use';
import { AssignBackupStorageConfig } from './AssignBackupStorageConfig';
import { formatBytes } from '../../xcluster/ReplicationUtils';

import { AccountLevelBackupEmpty, UniverseLevelBackupEmpty } from './BackupEmpty';
import { YBTable } from '../../common/YBTable';
import { find, isEmpty, get } from 'lodash';
import { fetchTablesInUniverse } from '../../../actions/xClusterReplication';
import { api } from '../../../redesign/features/universe/universe-form/utils/api';
import { AllowedTasks, TableTypeLabel } from '../../../redesign/helpers/dtos';
import { ybFormatDate } from '../../../redesign/helpers/DateUtils';
import {
  RbacValidator,
  customPermValidateFunction,
  hasNecessaryPerm
} from '../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../redesign/features/rbac/ApiAndUserPermMapping';
import { Action, Resource } from '../../../redesign/features/rbac';
import { TaskDetailSimpleComp } from '../../../redesign/features/tasks/components/TaskDetailSimpleComp';
import './BackupList.scss';

// eslint-disable-next-line @typescript-eslint/no-var-requires
const reactWidgets = require('react-widgets');
// eslint-disable-next-line @typescript-eslint/no-var-requires
const momentLocalizer = require('react-widgets-moment');
require('react-widgets/dist/css/react-widgets.css');

const { DateTimePicker } = reactWidgets;
momentLocalizer(moment);

const DEFAULT_SORT_COLUMN = 'createTime';
const DEFAULT_SORT_DIRECTION = 'DESC';

export const TIME_RANGE_OPTIONS = [
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

const MORE_FILTER_OPTIONS = [
  {
    label: 'Filter by:',
    options: [
      {
        label: 'Backups with deleted source universe',
        value: 'onlyShowDeletedUniverses'
      },
      {
        label: 'Backups with deleted config file',
        value: 'onlyShowDeletedConfigs'
      }
    ]
  }
];

export const DEFAULT_TIME_STATE: TIME_RANGE_STATE = {
  startTime: null,
  endTime: null,
  label: null
};

interface BackupListOptions {
  allowTakingBackup?: boolean;
  universeUUID?: string;
  allowedTasks: AllowedTasks;
}

const useTooltipStyles = makeStyles((theme) => ({
  customWidth: {
    maxWidth: 'none'
  },
  tooltipHeader: {
    padding: theme.spacing(1.5, 2),
    borderBottom: '1px solid #E5E5E9'
  },
  toolTipTitle: {
    fontWeight: 600,
    fontSize: 15
  },
  tooltipBody: {
    display: 'flex',
    flexDirection: 'column',
    padding: theme.spacing(2.5, 2)
  },
  windowContainer: {
    padding: theme.spacing(1.5, 2),
    backgroundColor: '#F7F9FB',
    borderRadius: '8px',
    border: '1px solid #E5E5E9',
    gap: '10px',
    margin: theme.spacing(0.75, 0)
  },
  dottedBorder: {
    borderBottom: '1px dotted #0B1117',
    width: 'fit-content'
  }
}));

export const BackupList: FC<BackupListOptions> = ({
  allowTakingBackup,
  universeUUID,
  allowedTasks
}) => {
  const [sizePerPage, setSizePerPage] = useState(10);
  const [page, setPage] = useState(1);
  const [searchText, setSearchText] = useState('');
  const [customStartTime, setCustomStartTime] = useState<Date | undefined>();
  const [customEndTime, setCustomEndTime] = useState<Date | undefined>();
  const [sortDirection, setSortDirection] = useState(DEFAULT_SORT_DIRECTION);

  const [showDeleteModal, setShowDeleteModal] = useState(false);
  const [showRestoreModal, setShowRestoreModal] = useState(false);
  const [showBackupCreateModal, setShowBackupCreateModal] = useState(false);
  const [showAssignConfigModal, setShowAssignConfigModal] = useState(false);
  const [showEditBackupModal, setShowEditBackupModal] = useState(false);
  const [isRestoreEntireBackup, setRestoreEntireBackup] = useState(false);
  const [incrementalBackupProps, setIncrementalBackupsProps] = useState<IncrementalBackupProps>({});

  const [selectedBackups, setSelectedBackups] = useState<IBackup[]>([]);
  const [status, setStatus] = useState<any[]>([BACKUP_STATUS_OPTIONS[0]]);
  const [moreFilters, setMoreFilters] = useState<any>([]);

  const featureFlags = useSelector((state: any) => state.featureFlags);

  const classes = useTooltipStyles();
  const isNewRestoreModalEnabled =
    featureFlags.test.enableNewRestoreModal || featureFlags.released.enableNewRestoreModal;

  const { data: runtimeConfigs, isLoading: runtimeConfigLoading } = useQuery(['runtimeConfigs', universeUUID], () => api.fetchRunTimeConfigs(true, universeUUID));

  const enableBackupPITR = !runtimeConfigLoading && isBackupPITREnabled(runtimeConfigs!);

  const timeReducer = (_state: TIME_RANGE_STATE, action: OptionTypeBase) => {
    if (action.label === 'Custom') {
      return { startTime: customStartTime, endTime: customEndTime, label: action.label };
    }
    if (action.label === 'All time') {
      return { startTime: null, endTime: null, label: action.label };
    }

    return {
      label: action.label,
      startTime: moment().subtract(action.value[0], action.value[1]).toDate(),
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
      moreFilters,
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
        moreFilters,
        universeUUID,
        storage_config_uuid
      ),
    {
      refetchInterval: BACKUP_REFETCH_INTERVAL,
      onSuccess(resp) {
        if (showDetails) {
          setShowDetails(
            resp.data.entities.find(
              (e: IBackup) =>
                e.commonBackupInfo.backupUUID === showDetails.commonBackupInfo.backupUUID
            ) ?? null
          );
        }
      }
    }
  );

  const { data: tablesInUniverse, isLoading: isTableListLoading } = useQuery(
    [universeUUID, 'tables'],
    () => {
      return fetchTablesInUniverse(universeUUID!);
    },
    {
      enabled: allowTakingBackup !== undefined && universeUUID !== undefined
    }
  );

  const [showDetails, setShowDetails] = useState<IBackup | null>(null);
  const storageConfigs = useSelector((reduxState: any) => reduxState.customer.configs);
  const currentUniverse = useSelector((reduxState: any) => reduxState.universe.currentUniverse);

  const [restoreDetails, setRestoreDetails] = useState<IBackup | null>(null);
  const [cancelBackupDetails, setCancelBackupDetails] = useState<IBackup | null>(null);

  const storageConfigsMap = useMemo(
    () => convertArrayToMap(storageConfigs?.data ?? [], 'configUUID', 'configName'),
    [storageConfigs]
  );

  const isFilterApplied = () => {
    return (
      searchText.length !== 0 ||
      status[0].value !== null ||
      // eslint-disable-next-line @typescript-eslint/prefer-nullish-coalescing
      timeRange.startTime ||
      // eslint-disable-next-line @typescript-eslint/prefer-nullish-coalescing
      timeRange.endTime ||
      moreFilters.length > 0
    );
  };

  const canCreateBackup = hasNecessaryPerm({
    ...ApiPermissionMap.CREATE_BACKUP,
    onResource: universeUUID ?? ''
  });

  const canDeleteBackup = hasNecessaryPerm(ApiPermissionMap.DELETE_BACKUP);

  const canRestoreBackup = customPermValidateFunction(
    (userPerm) =>
      find(userPerm, { actions: [Action.BACKUP_RESTORE], resourceType: Resource.UNIVERSE }) !==
      undefined
  );

  const canChangeRetentionPeriod = hasNecessaryPerm(ApiPermissionMap.EDIT_BACKUP);

  const getActions = (row: IBackup) => {
    if (row.commonBackupInfo.state === Backup_States.DELETED) {
      return '';
    }
    if (row.commonBackupInfo.state === Backup_States.IN_PROGRESS) {
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
        <RbacValidator
          customValidateFunction={(userPerm) =>
            find(userPerm, {
              actions: [Action.BACKUP_RESTORE],
              resourceType: Resource.UNIVERSE
            }) !== undefined
          }
          isControl
          overrideStyle={{ display: 'block' }}
        >
          <MenuItem
            disabled={
              row.commonBackupInfo.state !== Backup_States.COMPLETED ||
              !row.isStorageConfigPresent ||
              !canRestoreBackup
            }
            onClick={(e) => {
              e.stopPropagation();
              if (
                row.commonBackupInfo.state !== Backup_States.COMPLETED ||
                !row.isStorageConfigPresent ||
                !canRestoreBackup
              ) {
                return;
              }
              setRestoreEntireBackup(true);
              setRestoreDetails(row);
              setShowRestoreModal(true);
            }}
          >
            Restore Entire Backup
          </MenuItem>
        </RbacValidator>
        <RbacValidator
          accessRequiredOn={ApiPermissionMap.DELETE_BACKUP}
          isControl
          overrideStyle={{ display: 'block' }}
        >
          <MenuItem
            onClick={(e) => {
              e.stopPropagation();
              if (!row.isStorageConfigPresent || !canDeleteBackup) return;
              setSelectedBackups([row]);
              setShowDeleteModal(true);
            }}
            disabled={!row.isStorageConfigPresent || !canDeleteBackup}
            className="action-danger"
          >
            Delete Backup
          </MenuItem>
        </RbacValidator>
        <RbacValidator
          accessRequiredOn={ApiPermissionMap.EDIT_BACKUP}
          isControl
          overrideStyle={{ display: 'block' }}
        >
          <MenuItem
            onClick={(e) => {
              e.stopPropagation();
              if (
                row.commonBackupInfo.state !== Backup_States.COMPLETED ||
                !row.isStorageConfigPresent ||
                !canChangeRetentionPeriod
              ) {
                return;
              }

              setSelectedBackups([row]);
              setShowEditBackupModal(true);
            }}
            disabled={
              row.commonBackupInfo.state !== Backup_States.COMPLETED ||
              !row.isStorageConfigPresent ||
              !canChangeRetentionPeriod
            }
          >
            Change Retention Period
          </MenuItem>
        </RbacValidator>
      </DropdownButton>
    );
  };

  const backups: IBackup[] = backupsList?.data.entities.map((b: IBackup) => {
    const windowPITRArr = b.commonBackupInfo.responseList.filter(
      (r) => !isEmpty(r?.backupPointInTimeRestoreWindow)
    );
    const statusPITR = isEmpty(windowPITRArr) ? 'Not Enabled' : 'Enabled';
    return { ...b, backupUUID: b.commonBackupInfo.backupUUID, statusPITR, windowPITRArr };
  });

  const isBackupNotSucceeded = (state: Backup_States) =>
    [
      Backup_States.IN_PROGRESS,
      Backup_States.STOPPING,
      Backup_States.FAILED,
      Backup_States.FAILED_TO_DELETE,
      Backup_States.SKIPPED,
      Backup_States.STOPPED
    ].includes(state);

  if (!isFilterApplied() && backups?.length === 0) {
    return allowTakingBackup ? (
      <>
        <UniverseLevelBackupEmpty
          onActionButtonClick={() => {
            setShowBackupCreateModal(true);
          }}
          disabled={
            tablesInUniverse?.data.length === 0 ||
            currentUniverse?.data?.universeConfig?.takeBackups === 'false' ||
            currentUniverse?.data?.universeDetails?.universePaused ||
            !canCreateBackup
          }
          hasPerm={canCreateBackup}
        />
        <BackupCreateModal
          visible={showBackupCreateModal}
          onHide={() => {
            setShowBackupCreateModal(false);
          }}
          currentUniverseUUID={universeUUID}
          allowedTasks={allowedTasks}
        />
      </>
    ) : (
      <AccountLevelBackupEmpty />
    );
  }

  return (
    <Row className="backup-v2">
      <div className="backup-actions">
        <div className="search-and-filter">
          {!allowTakingBackup && (
            <>
              <div className="search-placeholder">
                <YBSearchInput
                  placeHolder="Search universe name"
                  onEnterPressed={(val: string) => setSearchText(val)}
                />
              </div>
              <span className="flex-divider" />
            </>
          )}
          <div className="status-filters">
            <YBMultiSelectRedesiged
              className="backup-status-filter"
              name="statuses"
              customLabel="Status:"
              placeholder="Status"
              isMulti={false}
              options={BACKUP_STATUS_OPTIONS}
              value={status}
              onChange={(value: any) => {
                setStatus(value ? [value] : []);
              }}
            />
            <YBMultiSelectRedesiged
              className="backup-status-more-filter"
              name="more-filters"
              placeholder="More Filters"
              customLabel="Filter by:"
              isMulti={false}
              options={MORE_FILTER_OPTIONS}
              isClearable={true}
              onChange={(value: any) => {
                setMoreFilters(value ? [value] : []);
              }}
            />
          </div>
        </div>
        <div className="actions-delete-filters no-padding">
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
                zIndex: 10,
                height: '325px'
              }),
              menuList: (base) => ({
                ...base,
                minHeight: '325px'
              })
            }}
            defaultValue={TIME_RANGE_OPTIONS.find((t) => t.label === 'All time')}
            maxMenuHeight={300}
          ></Select>
          <RbacValidator accessRequiredOn={ApiPermissionMap.DELETE_BACKUP} isControl>
            <YBButton
              btnText="Delete"
              btnIcon="fa fa-trash-o"
              onClick={() => setShowDeleteModal(true)}
              disabled={selectedBackups.length === 0}
            />
          </RbacValidator>
          {allowTakingBackup && (
            <>
              <RbacValidator
                accessRequiredOn={{
                  onResource: universeUUID,
                  ...ApiPermissionMap.CREATE_BACKUP
                }}
                isControl
              >
                <YBButton
                  loading={isTableListLoading}
                  btnText="Backup now"
                  onClick={() => {
                    setShowBackupCreateModal(true);
                  }}
                  btnClass="btn btn-orange backup-now-button"
                  btnIcon="fa fa-upload"
                  disabled={
                    tablesInUniverse?.data.length === 0 ||
                    currentUniverse?.data?.universeConfig?.takeBackups === 'false' ||
                    currentUniverse?.data?.universeDetails?.universePaused
                  }
                />
              </RbacValidator>
            </>
          )}
        </div>
      </div>
      <Row
        className={clsx('backup-list-table', {
          'account-level-view': !allowTakingBackup,
          'universe-level-view': allowTakingBackup
        })}
      >
        {isLoading && <YBLoading />}
        <YBTable
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
            selected: selectedBackups.map((b) => b.commonBackupInfo.backupUUID),
            onSelect: (row, isSelected) => {
              if (isSelected) {
                setSelectedBackups([...selectedBackups, row]);
              } else {
                setSelectedBackups(
                  selectedBackups.filter((b) => b.commonBackupInfo.backupUUID !== row.backupUUID)
                );
              }
            },
            onSelectAll: (isSelected, row) => {
              isSelected ? setSelectedBackups(row) : setSelectedBackups([]);
              return true;
            }
          }}
          trClassName={(row) =>
            `${find(selectedBackups, { backupUUID: row.backupUUID }) ? 'selected-row' : ''}`
          }
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
            hidden={allowTakingBackup}
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
            dataField="hasIncrementalBackups"
            dataFormat={(hasIncrementalBackups) =>
              hasIncrementalBackups ? 'Present' : 'Not Present'
            }
            width="20%"
          >
            Incremental
            <br />
            Backups
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="backupType"
            dataFormat={(backupType) => TableTypeLabel[backupType]}
            width="10%"
          >
            API Type
          </TableHeaderColumn>
          {enableBackupPITR && (
            <TableHeaderColumn
              dataField="statusPITR"
              dataFormat={(statusPITR, row) => {
                const startWindowTime = get(
                  row,
                  'windowPITRArr[0].backupPointInTimeRestoreWindow.timestampRetentionWindowStartMillis',
                  false
                );
                const endWindowTime = get(
                  row,
                  'windowPITRArr[0].backupPointInTimeRestoreWindow.timestampRetentionWindowEndMillis',
                  false
                );
                const windowExists = startWindowTime && endWindowTime;
                return (
                  <YBTooltip
                    interactive={true}
                    classes={{ tooltip: classes.customWidth }}
                    title={
                      windowExists ? (
                        <Box display="flex" flexDirection="column" width="auto">
                          <Box className={classes.tooltipHeader}>
                            <Typography className={classes.toolTipTitle}>Restore Window</Typography>
                          </Box>
                          <Box className={classes.tooltipBody}>
                            <Typography variant="body2">
                              You may restore to any time between:
                            </Typography>
                            <Box className={classes.windowContainer}>
                              <Typography variant="body2">
                                <b>{ybFormatDate(startWindowTime)}</b> to &nbsp;
                                <b>{ybFormatDate(endWindowTime)}</b>
                              </Typography>
                            </Box>
                          </Box>
                        </Box>
                      ) : (
                        ''
                      )
                    }
                  >
                    <div className={windowExists ? classes.dottedBorder : ''}>{statusPITR}</div>
                  </YBTooltip>
                );
              }}
              width="10%"
              dataSort
            >
              Point-in-Time
              <br />
              Restore
            </TableHeaderColumn>
          )}
          <TableHeaderColumn
            dataField="createTime"
            dataFormat={(_, row: IBackup) => ybFormatDate(row.commonBackupInfo.createTime)}
            width="20%"
            dataSort
          >
            Created At
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="expiryTime"
            dataFormat={(time) => (time ? ybFormatDate(time) : "Won't Expire")}
            width="20%"
          >
            Expiration
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="totalBackupSizeInBytes"
            dataFormat={(_, row: IBackup) => {
              return formatBytes(
                row.fullChainSizeInBytes || row.commonBackupInfo.totalBackupSizeInBytes
              );
            }}
            width="10%"
          >
            Size
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="lastBackupState"
            dataFormat={(lastBackupState, row: IBackup) => {
              return (
                <div onClick={(e) => e.stopPropagation()} className="backup-status">
                  <StatusBadge statusType={lastBackupState} />
                  {isBackupNotSucceeded(row.commonBackupInfo.state) && (
                    <TaskDetailSimpleComp taskUUID={row.commonBackupInfo.taskUUID} />
                  )}
                </div>
              );
            }}
            width="25%"
          >
            Last Status
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="actions"
            dataAlign="right"
            dataFormat={(_, row) => getActions(row)}
            columnClassName="yb-actions-cell no-border"
            width="10%"
          />
        </YBTable>
      </Row>
      <BackupDetails
        backupDetails={showDetails}
        onHide={() => setShowDetails(null)}
        storageConfigName={
          showDetails ? storageConfigsMap?.[showDetails?.commonBackupInfo.storageConfigUUID] : '-'
        }
        onDelete={() => {
          setSelectedBackups([showDetails] as IBackup[]);
          setShowDeleteModal(true);
        }}
        onRestore={(customDetails?: IBackup, incrementalBackupProps?: IncrementalBackupProps) => {
          setRestoreEntireBackup(incrementalBackupProps?.isRestoreEntireBackup ?? false);
          setIncrementalBackupsProps(incrementalBackupProps ?? {});
          setRestoreDetails(customDetails ?? showDetails);
          setShowRestoreModal(true);
        }}
        storageConfigs={storageConfigs}
        onAssignStorageConfig={() => {
          setShowAssignConfigModal(true);
        }}
        currentUniverseUUID={universeUUID}
        onEdit={() => {
          setSelectedBackups([showDetails] as IBackup[]);
          setShowEditBackupModal(true);
        }}
        tablesInUniverse={tablesInUniverse?.data}
      />
      <BackupDeleteModal
        backupsList={selectedBackups}
        visible={showDeleteModal}
        onHide={() => setShowDeleteModal(false)}
      />
      {!isNewRestoreModalEnabled && restoreDetails && (
        <BackupRestoreModal
          backup_details={restoreDetails}
          allowedTasks={allowedTasks}
          visible={showRestoreModal}
          isRestoreEntireBackup={isRestoreEntireBackup}
          onHide={() => {
            setShowRestoreModal(false);
          }}
        />
      )}
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
        allowedTasks={allowedTasks}
        currentUniverseUUID={universeUUID}
      />
      <AssignBackupStorageConfig
        visible={showAssignConfigModal}
        backup={showDetails}
        onHide={() => {
          setShowAssignConfigModal(false);
        }}
      />
      <BackupCreateModal
        allowedTasks={allowedTasks}
        visible={showEditBackupModal}
        onHide={() => setShowEditBackupModal(false)}
        currentUniverseUUID={selectedBackups[0]?.universeUUID}
        isEditBackupMode={true}
        isEditMode={true}
        isIncrementalBackup={selectedBackups[0]?.hasIncrementalBackups}
        isScheduledBackup={selectedBackups.length !== 0 && !selectedBackups[0].onDemand}
        editValues={
          selectedBackups[0] &&
          convertBackupToFormValues(
            selectedBackups[0],
            storageConfigs?.data.find((e: IStorageConfig) => {
              return e.configUUID === selectedBackups[0].commonBackupInfo.storageConfigUUID;
            })
          )
        }
      />
      {isNewRestoreModalEnabled &&
        restoreDetails &&
        (!enableBackupPITR ? (
          <BackupRestoreNewModal
            backupDetails={restoreDetails as any}
            visible={true}
            onHide={() => {
              setRestoreDetails(null);
              setRestoreEntireBackup(false);
              setIncrementalBackupsProps({});
            }}
            incrementalBackupProps={incrementalBackupProps}
          />
        ) : (
          <BackupRestoreModalWithPITR
            backupDetails={restoreDetails as any}
            onHide={() => {
              setRestoreDetails(null);
              setRestoreEntireBackup(false);
              setIncrementalBackupsProps({});
            }}
            visible={true}
            incrementalBackupProps={incrementalBackupProps}
          />
        ))}
    </Row>
  );
};
