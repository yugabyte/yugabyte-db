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
import { Backup_States, IBackup, Keyspace_Table, TableType } from '.';
import { StatusBadge } from '../common/badge/StatusBadge';
import { YBButton } from '../common/forms/fields';
import './BackupDetails.scss';
import {
  calculateDuration,
  FormatUnixTimeStampTimeToTimezone,
  RevealBadge,
  SearchInput
} from './BackupUtils';
import { YCQLTableList, YSQLTableList } from './BackupTableList';
interface BackupDetailsProps {
  backup_details: IBackup | null;
  onHide: () => void;
  storageConfigName: string;
  onDelete: () => void;
  onRestore: (backup?: IBackup) => void;
  storageConfigs: {
    data?: any[];
  };
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
  backup_details,
  onHide,
  storageConfigName,
  onRestore,
  onDelete,
  storageConfigs
}) => {
  const [searchKeyspaceText, setSearchKeyspaceText] = useState('');

  if (!backup_details) return null;
  const storageConfig = storageConfigs?.data?.find(
    (config) => config.configUUID === backup_details.storageConfigUUID
  );
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
                backup_details.state === Backup_States.DELETED ||
                backup_details.state === Backup_States.DELETE_IN_PROGRESS ||
                backup_details.state === Backup_States.QUEUED_FOR_DELETION
              }
            />
            <YBButton
              btnText="Restore Entire Backup"
              onClick={() => onRestore()}
              disabled={backup_details.state !== Backup_States.COMPLETED}
            />
          </Row>
          <Row className="backup-details-info">
            <div className="name-and-status">
              <div>
                <div className="header-text">
                  Source Universe Name &nbsp;&nbsp;&nbsp;
                  <RevealBadge label="Show UUID" textToShow={backup_details.universeUUID} />
                </div>
                <div className="universeLink">
                  <Link target="_blank" to={`/universes/${backup_details.universeUUID}`}>
                    {backup_details.universeName}
                  </Link>
                </div>
                {!backup_details.isUniversePresent && <div>{SOURCE_UNIVERSE_DELETED_MSG}</div>}
              </div>
              <div>
                <div className="header-text">Backup Status</div>
                <StatusBadge statusType={backup_details.state as any} />
              </div>
            </div>
            <div className="details-rest">
              <div>
                <div className="header-text">Backup Type</div>
                <div>{backup_details.backupType ? 'On Demand' : 'Scheduled'}</div>
              </div>
              <div>
                <div className="header-text">Table Type</div>
                <div>{backup_details.backupType}</div>
              </div>
              <div>
                <div className="header-text">Create Time</div>
                <div>
                  <FormatUnixTimeStampTimeToTimezone timestamp={backup_details.createTime} />
                </div>
              </div>
              <div>
                <div className="header-text">Expiration</div>
                <div>
                  <FormatUnixTimeStampTimeToTimezone timestamp={backup_details.expiryTime} />
                </div>
              </div>
              <div>
                <div className="header-text">Duration</div>
                <div>{calculateDuration(backup_details.createTime, backup_details.updateTime)}</div>
              </div>
              <div>
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
            </div>
          </Row>
          {backup_details.state !== Backup_States.FAILED && (
            <Row className="tables-list">
              <Col lg={6} className="no-padding">
                <SearchInput
                  placeHolder="Search keyspace name"
                  onValueChanged={(e: React.ChangeEvent<HTMLInputElement>) => {
                    setSearchKeyspaceText(e.target.value);
                  }}
                />
              </Col>

              <Col lg={12} className="no-padding">
                {backup_details.backupType === TableType.YQL_TABLE_TYPE ? (
                  <YCQLTableList
                    backup={backup_details}
                    keyspaceSearch={searchKeyspaceText}
                    onRestore={(tablesList: Keyspace_Table[]) => {
                      onRestore({
                        ...backup_details,
                        responseList: tablesList
                      });
                    }}
                  />
                ) : (
                  <YSQLTableList
                    backup={backup_details}
                    keyspaceSearch={searchKeyspaceText}
                    onRestore={(tablesList: Keyspace_Table[]) => {
                      onRestore({
                        ...backup_details,
                        responseList: tablesList
                      });
                    }}
                  />
                )}
              </Col>
            </Row>
          )}
        </div>
      </div>
    </div>
  );
};
