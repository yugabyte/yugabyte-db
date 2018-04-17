// Copyright (c) YugaByte, Inc.

import React, { Component, Fragment } from 'react';
import PropTypes from 'prop-types';
import { DropdownButton } from 'react-bootstrap';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { YBPanelItem } from '../../panels';
import { getPromiseState } from 'utils/PromiseUtils';
import { timeFormatter, successStringFormatter } from 'utils/TableFormatters';
import { YBLoadingIcon } from '../../common/indicators';
import { TableAction } from '../../tables';

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
      // Show action button to restore/delete only when the backup is
      // create and which has completed successfully.
      backupInfo.showActions = (backupInfo.actionType === "CREATE" &&
                                backupInfo.status === "Completed");
      return backupInfo;
    });

    const formatActionButtons = function(item, row) {
      if (row.showActions) {
        return (
          <DropdownButton className="btn btn-default" title="Actions" id="bg-nested-dropdown" pullRight>
            <TableAction currentRow={row} actionType="restore-backup" />
          </DropdownButton>
        );
      }
    };

    return (
      <YBPanelItem
        header={
          <Fragment>
            <h2 className="task-list-header content-title pull-left">{title}</h2>
            <TableAction actionType="create-backup" isMenuItem={false} className="pull-right"/>
          </Fragment>

        }
        body={
          <BootstrapTable data={backupInfos} pagination={true} className="backup-list-table">
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
            <TableHeaderColumn dataField={"actions"} columnClassName={"yb-actions-cell"}
                               dataFormat={formatActionButtons}>
              Actions
            </TableHeaderColumn>
          </BootstrapTable>
        }
      />
    );
  }

}
