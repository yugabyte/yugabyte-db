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
import { Backup_States, IBackup, Keyspace_Table } from '..';
import { StatusBadge } from '../../common/badge/StatusBadge';
import { YBButton } from '../../common/forms/fields';
import {
  convertBackupToFormValues,
  FormatUnixTimeStampTimeToTimezone,
  RevealBadge
} from '../common/BackupUtils';
import {
  IncrementalTableBackupList,
  YCQLTableList,
  YSQLTableList,
  YSQLTableProps
} from './BackupTableList';
import { YBSearchInput } from '../../common/forms/fields/YBSearchInput';
import { TableType, TABLE_TYPE_MAP } from '../../../redesign/helpers/dtos';
import { isFunction } from 'lodash';
import { formatBytes } from '../../xcluster/ReplicationUtils';
import { useQuery } from 'react-query';
import { getKMSConfigs } from '../common/BackupAPI';

import { BackupCreateModal } from './BackupCreateModal';
import './BackupDetails.scss';

interface BackupDetailsProps {
  backupDetails: IBackup | null;
  onHide: () => void;
  storageConfigName: string;
  onDelete: () => void;
  onRestore: (backup?: IBackup) => void;
  storageConfigs: {
    data?: any[];
  };
  hideRestore?: boolean;
  onAssignStorageConfig?: () => void;
  currentUniverseUUID?: string;
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
  storageConfigs,
  hideRestore = false,
  onAssignStorageConfig,
  currentUniverseUUID
}) => {
  const [searchKeyspaceText, setSearchKeyspaceText] = useState('');
  const [showCreateModal, setShowCreateModal] = useState(false);

  const [formValues, setFormValues] = useState<Record<string, any>>();

  const { data: kmsConfigs } = useQuery(['kms_configs'], () => getKMSConfigs(), {
    enabled: backupDetails?.kmsConfigUUID !== undefined
  });

  const kmsConfig = kmsConfigs
    ? kmsConfigs.find((config: any) => {
        return config.metadata.configUUID === backupDetails?.kmsConfigUUID;
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
    if (
      backupDetails.backupType === TableType.YQL_TABLE_TYPE ||
      backupDetails.backupType === TableType.REDIS_TABLE_TYPE
    ) {
      TableListComponent = YCQLTableList;
    } else {
      TableListComponent = YSQLTableList;
    }
  }

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
            {!hideRestore && (
              <YBButton
                btnText="Restore Entire Backup"
                onClick={() => onRestore()}
                disabled={
                  backupDetails.commonBackupInfo.state !== Backup_States.COMPLETED ||
                  !backupDetails.isStorageConfigPresent
                }
              />
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
                <div>{backupDetails.backupType ? 'On Demand' : 'Scheduled'}</div>
              </div>
              <div>
                <div className="header-text">Table Type</div>
                <div>{TABLE_TYPE_MAP[backupDetails.backupType]}</div>
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
              <div></div>
              <div>
                <div className="header-text">Created At</div>
                <div>
                  <FormatUnixTimeStampTimeToTimezone
                    timestamp={backupDetails.commonBackupInfo.createTime}
                  />
                </div>
              </div>
              <div>
                <div className="header-text">Expiration</div>
                <div>
                  <FormatUnixTimeStampTimeToTimezone timestamp={backupDetails.expiryTime} />
                </div>
              </div>
              <span className="flex-divider" />
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
                <div>{kmsConfig ? kmsConfig.label : '-'}</div>
              </div>
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
                  <YBButton
                    btnText="Add Incremental Backup"
                    btnIcon="fa fa-plus"
                    className="add-increment-backup-btn"
                    onConfirm={() => {}}
                    onClick={() => {
                      setFormValues(convertBackupToFormValues(backupDetails, storageConfig));
                      setShowCreateModal(true);
                    }}
                  />
                </Col>
              )}

              <Col lg={12} className="no-padding">
                <TableListComponent
                  backup={backupDetails}
                  keyspaceSearch={searchKeyspaceText}
                  onRestore={(tablesList: Keyspace_Table[]) => {
                    onRestore({
                      ...backupDetails,
                      commonBackupInfo: {
                        ...backupDetails.commonBackupInfo,
                        responseList: tablesList
                      }
                    });
                  }}
                  hideRestore={hideRestore}
                />
              </Col>
            </Row>
          )}
        </div>
      </div>
      <BackupCreateModal
        visible={showCreateModal}
        onHide={() => {
          setShowCreateModal(false);
        }}
        editValues={formValues}
        currentUniverseUUID={currentUniverseUUID}
        isIncrementalBackup={true}
      />
    </div>
  );
};
