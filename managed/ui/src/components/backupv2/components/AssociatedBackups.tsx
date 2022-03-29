/*
 * Created on Tue Mar 22 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { FC, useEffect, useState } from 'react';
import { Col, Row } from 'react-bootstrap';
import { BootstrapTable, RemoteObjSpec, TableHeaderColumn } from 'react-bootstrap-table';
import { useQuery } from 'react-query';
import { StatusBadge } from '../../common/badge/StatusBadge';
import { YBModal } from '../../common/forms/fields';
import { YBSearchInput } from '../../common/forms/fields/YBSearchInput';
import { YBLoading } from '../../common/indicators';
import { getBackupsList } from '../common/BackupAPI';
import { ENTITY_NOT_AVAILABLE, FormatUnixTimeStampTimeToTimezone } from '../common/BackupUtils';
import { Backup_States, IBackup } from '../common/IBackup';
import './AssociatedBackups.scss';

interface AssociatedBackupsProps {
  visible: boolean;
  onHide: () => void;
  storageConfigUUID: string;
}

const DEFAULT_SORT_COLUMN = 'createTime';
const DEFAULT_SORT_DIRECTION = 'DESC';

export const AssociatedBackups: FC<AssociatedBackupsProps> = ({
  visible,
  onHide,
  storageConfigUUID
}) => {
  const [sizePerPage, setSizePerPage] = useState(10);
  const [page, setPage] = useState(1);
  const [searchText, setSearchText] = useState('');

  const { data: backupsList, isLoading } = useQuery(
    ['associated_backups', (page - 1) * sizePerPage, sizePerPage, searchText],
    () =>
      getBackupsList(
        (page - 1) * sizePerPage,
        sizePerPage,
        searchText,
        { startTime: undefined, endTime: undefined, label: undefined },
        [],
        DEFAULT_SORT_COLUMN,
        DEFAULT_SORT_DIRECTION,
        undefined,
        storageConfigUUID
      ),
    {
      enabled: visible
    }
  );

  //clear searchText on entry/exit
  useEffect(() => setSearchText(''), [visible]);

  const associatedBackups: IBackup[] = backupsList?.data.entities;

  return (
    <YBModal
      title={'Associated backups'}
      visible={visible}
      onHide={onHide}
      dialogClassName="associated-backups-modal"
      size="large"
      onFormSubmit={(event: any) => {
        //prevent parent form from being submitted
        event.stopPropagation();
        if (event.target.innerText === 'OK') {
          onHide();
        }
      }}
    >
      <Row>
        <Col lg={12} className="no-padding">
          <YBSearchInput
            placeHolder="Search universe name"
            onEnterPressed={(val: string) => setSearchText(val)}
          />
        </Col>
      </Row>
      <Row>
        <Col lg={12} className="associated-backup-list-table">
          {isLoading ? (
            <YBLoading />
          ) : (
            <BootstrapTable
              data={associatedBackups}
              options={{
                sizePerPage,
                onSizePerPageList: setSizePerPage,
                page,
                prePage: 'Prev',
                nextPage: 'Next',
                onPageChange: (page) => setPage(page)
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
            >
              <TableHeaderColumn dataField="backupUUID" isKey={true} hidden={true} />
              <TableHeaderColumn
                dataField="universeUUID"
                dataFormat={(_name, row: IBackup) =>
                  row.universeName ? row.universeName : ENTITY_NOT_AVAILABLE
                }
              >
                Source Universe Name
              </TableHeaderColumn>
              <TableHeaderColumn
                dataField="createTime"
                dataFormat={(time) => <FormatUnixTimeStampTimeToTimezone timestamp={time} />}
              >
                Created At
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
              >
                Status
              </TableHeaderColumn>
            </BootstrapTable>
          )}
        </Col>
      </Row>
    </YBModal>
  );
};
