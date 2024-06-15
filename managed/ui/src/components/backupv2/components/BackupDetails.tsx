/*
 * Created on Wed Feb 16 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { FC, useState } from 'react';
import { Col, Row } from 'react-bootstrap';
import { Link } from 'react-router';
import { Backup_States, IBackup, ICommonBackupInfo, ITable, Keyspace_Table } from '..';
import { fetchIncrementalBackup } from '.././common/BackupAPI';
import { StatusBadge } from '../../common/badge/StatusBadge';
import { YBButton } from '../../common/forms/fields';
import { BACKUP_REFETCH_INTERVAL, RevealBadge, calculateDuration } from '../common/BackupUtils';
import {
  IncrementalTableBackupList,
  YCQLTableList,
  YSQLTableList,
  YSQLTableProps
} from './BackupTableList';
import { YBSearchInput } from '../../common/forms/fields/YBSearchInput';
import { TableType, TableTypeLabel } from '../../../redesign/helpers/dtos';
import { find, findIndex, isFunction } from 'lodash';
import { formatBytes } from '../../xcluster/ReplicationUtils';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { getKMSConfigs, addIncrementalBackup } from '../common/BackupAPI';

import { YBConfirmModal } from '../../modals';
import { toast } from 'react-toastify';
import { createErrorMessage } from '../../../utils/ObjectUtils';
import { ybFormatDate } from '../../../redesign/helpers/DateUtils';
import { YBLoadingCircleIcon } from '../../common/indicators';
import { handleCACertErrMsg } from '../../customCACerts';
import { RbacValidator } from '../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../redesign/features/rbac/ApiAndUserPermMapping';
import { Action, Resource } from '../../../redesign/features/rbac';
import './BackupDetails.scss';

export type IncrementalBackupProps = {
  isRestoreEntireBackup?: boolean; // if the restore entire backup button is clicked
  incrementalBackupUUID?: string; // if restore to point button is clicked
  singleKeyspaceRestore?: boolean; // if restore button is clicked inside the incremental backup
  kmsConfigUUID?: string;
};
interface BackupDetailsProps {
  backupDetails: IBackup | null;
  onHide: () => void;
  storageConfigName: string;
  onDelete: () => void;
  onRestore: (backup?: IBackup, incrementalProps?: IncrementalBackupProps) => void;
  storageConfigs: {
    data?: any[];
  };
  onEdit?: () => void;
  hideRestore?: boolean;
  onAssignStorageConfig?: () => void;
  currentUniverseUUID?: string;
  tablesInUniverse?: ITable[];
}
const SOURCE_UNIVERSE_DELETED_MSG = (
  <span className="alert-message warning">
    <i className="fa fa-warning" /> Source universe for this backup has been deleted
  </span>
);
const STORAGE_CONFIG_DELETED_MSG = (
  <span className="alert-message warning">
    <i className="fa fa-warning" /> Not available. The storage config associated with this backup
    has been deleted.
  </span>
);
export const BackupDetails: FC<BackupDetailsProps> = ({
  backupDetails,
  onHide,
  storageConfigName,
  onRestore,
  onDelete,
  onEdit,
  storageConfigs,
  hideRestore = false,
  onAssignStorageConfig,
  currentUniverseUUID,
  tablesInUniverse
}) => {
  const [searchKeyspaceText, setSearchKeyspaceText] = useState('');
  const [showAddIncrementalBackupModal, setShowAddIncrementalBackupModal] = useState(false);
  const queryClient = useQueryClient();
  const { data: kmsConfigs } = useQuery(['kms_configs'], () => getKMSConfigs(), {
    enabled: backupDetails?.kmsConfigUUID !== undefined
  });

  const { data: incrementalBackups, isLoading } = useQuery(
    ['incremental_backups', backupDetails?.commonBackupInfo.baseBackupUUID],
    () => fetchIncrementalBackup(backupDetails!.commonBackupInfo?.baseBackupUUID),
    {
      refetchInterval: BACKUP_REFETCH_INTERVAL,
      enabled: backupDetails !== null && backupDetails.hasIncrementalBackups
    }
  );

  const doAddIncrementalBackup = useMutation(
    () => {
      let responseList: Keyspace_Table[] = [];

      if (!backupDetails?.isFullBackup) {
        responseList = backupDetails!.commonBackupInfo.responseList;
      }

      if (backupDetails!.backupType === TableType.YQL_TABLE_TYPE) {
        const atleastOneTableAvailableForBackup = responseList.every((r) => {
          if (r.allTables) return true;
          return r.tableUUIDList?.some((tableUUID) => find(tablesInUniverse, { tableUUID }));
        });

        if (!atleastOneTableAvailableForBackup) {
          return Promise.reject({
            response: {
              data: { error: `None of selected tables to backup found in the keyspace` }
            }
          });
        }

        const allTableAvailableForBackup = responseList.every((r) => {
          if (r.allTables) return true;
          return r.tableUUIDList?.every((tableUUID) => find(tablesInUniverse, { tableUUID }));
        });

        if (!allTableAvailableForBackup) {
          toast.warning(
            `One or more of selected tables to backup do not exist in keyspace. Proceeding backup without non-existent table.`,
            { autoClose: false }
          );
        }

        responseList = responseList.map((r) => {
          const backupTablesPresentInUniverse = r.tablesList.filter(
            (tableName, index) =>
              find(tablesInUniverse, {
                tableName,
                keySpace: r.keyspace,
                tableUUID: r.tableUUIDList?.[index]
              })?.tableName
          );

          return {
            ...r,
            tableNameList: r.allTables ? [] : backupTablesPresentInUniverse,
            tableUUIDList: r.allTables
              ? []
              : backupTablesPresentInUniverse.map(
                (tableName) =>
                  find(tablesInUniverse, { tableName, keySpace: r.keyspace })?.tableUUID ?? ''
              )
          };
        });
      }

      const uniqueKeyspaceResponseList: any[] = [];

      responseList.forEach((r) => {
        const indexOfKeyspace = findIndex(uniqueKeyspaceResponseList, { keyspace: r.keyspace });
        if (indexOfKeyspace !== -1) {
          uniqueKeyspaceResponseList[indexOfKeyspace] = {
            ...uniqueKeyspaceResponseList[indexOfKeyspace],
            tableNameList: [
              ...uniqueKeyspaceResponseList[indexOfKeyspace].tableNameList,
              ...r.tableNameList!
            ],
            tableUUIDList: [
              ...uniqueKeyspaceResponseList[indexOfKeyspace].tableUUIDList,
              ...r.tableUUIDList!
            ]
          };
        } else {
          uniqueKeyspaceResponseList.push(r);
        }
      });

      return addIncrementalBackup({
        ...backupDetails!,
        commonBackupInfo: {
          ...backupDetails!.commonBackupInfo,
          responseList: uniqueKeyspaceResponseList
        }
      });
    },
    {
      onSuccess: () => {
        toast.success('Incremental backup added successfully!');
        queryClient.invalidateQueries([
          'incremental_backups',
          backupDetails!.commonBackupInfo.baseBackupUUID
        ]);
        setShowAddIncrementalBackupModal(false);
      },
      onError: (resp: any) => {
        !handleCACertErrMsg(resp) && toast.error(createErrorMessage(resp));
      }
    }
  );

  const kmsConfig = kmsConfigs
    ? kmsConfigs.find((config: any) => {
      return config.metadata.configUUID === backupDetails?.commonBackupInfo?.kmsConfigUUID;
    })
    : undefined;

  if (!backupDetails) return null;

  const storageConfig = storageConfigs?.data?.find(
    (config) => config.configUUID === backupDetails.commonBackupInfo.storageConfigUUID
  );

  let TableListComponent: React.FC<YSQLTableProps> = () => null;

  if (backupDetails.hasIncrementalBackups) {
    TableListComponent = IncrementalTableBackupList;
  } else {
    // eslint-disable-next-line no-lonely-if
    if (
      backupDetails.backupType === TableType.YQL_TABLE_TYPE ||
      backupDetails.backupType === TableType.REDIS_TABLE_TYPE
    ) {
      TableListComponent = YCQLTableList;
    } else {
      TableListComponent = YSQLTableList;
    }
  }

  if (isLoading) return <YBLoadingCircleIcon />;

  return (
    <div id="universe-tab-panel-pane-queries" className={'backup-details-panel'}>
      <div className={`side-panel`}>
        <div className="side-panel__header">
          <span
            className="side-panel__icon--close"
            onClick={() => {
              onHide();
            }}
          >
            <i className="fa fa-close" />
          </span>
          <div className="side-panel__title">Backup Details</div>
        </div>
        <div className="side-panel__content">
          <Row className="backup-details-actions">
            <RbacValidator
              accessRequiredOn={ApiPermissionMap.DELETE_BACKUP}
              isControl
              popOverOverrides={{ zIndex: 10000 }}
            >
              <YBButton
                btnText="Delete"
                btnIcon="fa fa-trash-o"
                onClick={() => onDelete()}
                disabled={
                  backupDetails.commonBackupInfo.state === Backup_States.DELETED ||
                  backupDetails.commonBackupInfo.state === Backup_States.DELETE_IN_PROGRESS ||
                  backupDetails.commonBackupInfo.state === Backup_States.QUEUED_FOR_DELETION ||
                  !backupDetails.isStorageConfigPresent
                }
              />
            </RbacValidator>
            {!hideRestore && (
              <RbacValidator
                customValidateFunction={(userPerm) => find(userPerm, { actions: [Action.BACKUP_RESTORE], resourceType: Resource.UNIVERSE }) !== undefined}
                isControl
                popOverOverrides={{ zIndex: 10000 }}
              >
                <YBButton
                  btnText="Restore Entire Backup"
                  btnIcon="fa fa-share"
                  onClick={() => {
                    if (backupDetails.hasIncrementalBackups) {
                      if (incrementalBackups?.data) {
                        const recentBackup = incrementalBackups.data.filter(
                          (e: ICommonBackupInfo) => e.state === Backup_States.COMPLETED
                        )[0];
                        onRestore(
                          { ...backupDetails, commonBackupInfo: recentBackup },
                          {
                            isRestoreEntireBackup: true,
                            incrementalBackupUUID: recentBackup.backupUUID,
                            singleKeyspaceRestore: false
                          }
                        );
                      }
                    } else {
                      onRestore(undefined, {
                        isRestoreEntireBackup: true,
                        singleKeyspaceRestore: false
                      });
                    }
                  }}
                  disabled={
                    backupDetails.commonBackupInfo.state !== Backup_States.COMPLETED ||
                    !backupDetails.isStorageConfigPresent
                  }
                />
              </RbacValidator>
            )}
            {onEdit && (
              <RbacValidator
                accessRequiredOn={ApiPermissionMap.EDIT_BACKUP}
                isControl
                popOverOverrides={{ zIndex: 10000 }}
              >
                <YBButton
                  btnText="Change Retention Period"
                  btnIcon="fa fa-pencil"
                  onClick={() => onEdit()}
                  disabled={
                    backupDetails.commonBackupInfo.state !== Backup_States.COMPLETED ||
                    !backupDetails.isStorageConfigPresent
                  }
                />
              </RbacValidator>
            )}
          </Row>
          <Row className="backup-details-info">
            <div className="name-and-status">
              <div>
                <div className="header-text">
                  Source Universe Name &nbsp;&nbsp;&nbsp;
                  <RevealBadge label="Show UUID" textToShow={backupDetails.universeUUID} />
                </div>

                {backupDetails.isUniversePresent ? (
                  <div className="universeLink">
                    <Link target="_blank" to={`/universes/${backupDetails.universeUUID}`}>
                      {backupDetails.universeName}
                    </Link>
                  </div>
                ) : (
                  backupDetails.universeName
                )}

                {!backupDetails.isUniversePresent && <div>{SOURCE_UNIVERSE_DELETED_MSG}</div>}
              </div>
              <div>
                <div className="header-text">Backup Status</div>
                <StatusBadge statusType={backupDetails.commonBackupInfo.state as any} />
              </div>
            </div>
            <div className="details-rest">
              <div>
                <div className="header-text">Backup Type</div>
                <div>{backupDetails.onDemand ? 'On Demand' : 'Scheduled'}</div>
              </div>
              <div>
                <div className="header-text">Table Type</div>
                <div>{TableTypeLabel[backupDetails.backupType]}</div>
              </div>
              <div>
                <div className="header-text">Size</div>
                <div>
                  {formatBytes(
                    backupDetails.fullChainSizeInBytes ||
                    backupDetails.commonBackupInfo.totalBackupSizeInBytes
                  )}
                </div>
              </div>
              {!backupDetails.hasIncrementalBackups && (
                <div>
                  <div className="header-text">Duration</div>
                  <div>
                    {calculateDuration(
                      backupDetails?.commonBackupInfo?.createTime,
                      backupDetails?.commonBackupInfo?.completionTime
                    )}
                  </div>
                </div>
              )}
              <div>
                <div className="header-text">Created At</div>
                <div>{ybFormatDate(backupDetails.commonBackupInfo.createTime)}</div>
              </div>
              <div>
                <div className="header-text">Expiration</div>
                <div>{backupDetails.expiryTime ? ybFormatDate(backupDetails.expiryTime) : "Won't Expire"}</div>
              </div>
              <div className="details-storage-config">
                <div className="header-text">Storage Config</div>
                <div className="universeLink">
                  <Link
                    target="_blank"
                    to={`/config/backup/${storageConfig ? storageConfig.name.toLowerCase() : ''}`}
                  >
                    {storageConfigName}
                  </Link>
                </div>
                {!storageConfigName && STORAGE_CONFIG_DELETED_MSG}
              </div>
              <div>
                <div className="header-text">KMS Config</div>
                <div>{kmsConfig ? kmsConfig.metadata?.name : '-'}</div>
              </div>
              {!backupDetails.onDemand && (
                <div>
                  <div className="header-text">Schedule Name</div>
                  <div>{backupDetails.scheduleName}</div>
                </div>
              )}
            </div>
            {!storageConfigName && (
              <span className="assign-config-msg">
                <span>
                  In order to <b>Delete</b> or <b>Restore</b> this backup you must first assign a
                  new storage config to this backup.
                </span>
                <YBButton
                  btnText="Assign storage config"
                  onClick={() => {
                    if (isFunction(onAssignStorageConfig)) {
                      onAssignStorageConfig();
                    }
                  }}
                />
              </span>
            )}
          </Row>
          {backupDetails.commonBackupInfo.state !== Backup_States.FAILED && (
            <Row className="tables-list">
              <Col lg={6} className="no-padding">
                <YBSearchInput
                  placeHolder="Search keyspace name"
                  onValueChanged={(e: React.ChangeEvent<HTMLInputElement>) => {
                    setSearchKeyspaceText(e.target.value);
                  }}
                />
              </Col>
              {currentUniverseUUID && backupDetails.isStorageConfigPresent && (
                <Col lg={6} className="no-padding">
                  <RbacValidator
                    customValidateFunction={(userPerm) => find(userPerm, { actions: [Action.BACKUP_RESTORE], resourceType: Resource.UNIVERSE }) !== undefined}
                    overrideStyle={{
                      display: 'unset'
                    }}
                    isControl
                    popOverOverrides={{ zIndex: 10000 }}
                  >
                    <YBButton
                      btnText="Add Incremental Backup"
                      btnIcon="fa fa-plus"
                      className="add-increment-backup-btn"
                      disabled={backupDetails.commonBackupInfo.state !== Backup_States.COMPLETED}
                      onClick={() => {
                        setShowAddIncrementalBackupModal(true);
                      }}
                    />
                  </RbacValidator>
                </Col>
              )}

              <Col lg={12} className="no-padding">
                <TableListComponent
                  backup={backupDetails}
                  keyspaceSearch={searchKeyspaceText}
                  onRestore={(
                    tablesList: Keyspace_Table[],
                    incrementalBackupProps: IncrementalBackupProps
                  ) => {
                    const commonBackupInfo = {
                      ...backupDetails.commonBackupInfo,
                      responseList: tablesList
                    };
                    if (incrementalBackupProps.kmsConfigUUID)
                      commonBackupInfo.kmsConfigUUID = incrementalBackupProps.kmsConfigUUID;
                    onRestore(
                      {
                        ...backupDetails,
                        commonBackupInfo
                      },
                      {
                        isRestoreEntireBackup: incrementalBackupProps.isRestoreEntireBackup,
                        incrementalBackupUUID: incrementalBackupProps.incrementalBackupUUID,
                        singleKeyspaceRestore: incrementalBackupProps.singleKeyspaceRestore
                      }
                    );
                  }}
                  hideRestore={hideRestore}
                />
              </Col>
            </Row>
          )}
        </div>
      </div>
      <YBConfirmModal
        name="add-incremental-modal"
        title="Add Incremental Backup"
        visibleModal={showAddIncrementalBackupModal}
        currentModal={true}
        modalClassname="backup-modal"
        onConfirm={() => doAddIncrementalBackup.mutate()}
        hideConfirmModal={() => setShowAddIncrementalBackupModal(false)}
      >
        You are about to add an incremental backup to your existing backup. This will back up only
        the data that has changed since your full backup.
      </YBConfirmModal>
    </div>
  );
};
