// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { YBPanelItem } from '../../panels';
import { getPromiseState } from 'utils/PromiseUtils';
import { timeFormatter, successStringFormatter } from 'utils/TableFormatters';
import { YBLoadingIcon } from '../../common/indicators';

import './ListBackups.scss';

export default class ListBackups extends Component {
  static defaultProps = {
    title : "Backups"
  }

  static propTypes  = {
    currentUniverse: PropTypes.object.isRequired
  }

  componentWillMount() {
    const { currentUniverse: { universeUUID }} = this.props;
    this.props.fetchUniverseBackups(universeUUID);
  }

  componentWillUnMount() {
    this.props.resetUniverseBackups();
  }

  render() {
    const { universeBackupList, title } = this.props;
    if (getPromiseState(universeBackupList).isLoading() ||
        getPromiseState(universeBackupList).isInit()) {
      return <YBLoadingIcon size="medium" />;
    }

    const backupInfos = universeBackupList.data.map((b) => {
      const backupInfo = b.backupInfo;
      backupInfo.backupUUID = b.backupUUID;
      backupInfo.status = b.state;
      backupInfo.createTime = b.createTime;
      return backupInfo;
    });

    return (
      <YBPanelItem
        header={
          <h2 className="task-list-header content-title">{title}</h2>
        }
        body={
          <BootstrapTable data={backupInfos} pagination={true} className="backup-list-table"
                          search multiColumnSearch searchPlaceholder='Search by Table Name or State'>
            <TableHeaderColumn dataField="backupUUID" isKey={true} hidden={true}/>
            <TableHeaderColumn dataField="keyspace" dataSort
                              columnClassName="no-border name-column" className="no-border">
              Keyspace
            </TableHeaderColumn>
            <TableHeaderColumn dataField="tableName" dataSort
                              columnClassName="no-border name-column" className="no-border">
              Table Name
            </TableHeaderColumn>
            <TableHeaderColumn dataField="createTime" dataFormat={timeFormatter} dataSort
                              columnClassName="no-border " className="no-border"
                              dataAlign="left">
              Created At
            </TableHeaderColumn>
            <TableHeaderColumn dataField="status" dataSort
                              columnClassName="no-border name-column" className="no-border"
                              dataFormat={successStringFormatter} >
              Status
            </TableHeaderColumn>
            <TableHeaderColumn dataField="storageLocation"
                              columnClassName="no-border storage-cell"
                              className="no-border storage-cell" >
              Storage Location
            </TableHeaderColumn>
          </BootstrapTable>
        }
      />
    );
  }

}
