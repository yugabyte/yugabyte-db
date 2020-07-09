// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';
import { DropdownButton } from 'react-bootstrap';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { YBPanelItem } from '../../panels';
import { getPromiseState } from '../../../utils/PromiseUtils';
import { isAvailable } from '../../../utils/LayoutUtils';
import { timeFormatter, successStringFormatter } from '../../../utils/TableFormatters';
import { YBLoadingCircleIcon } from '../../common/indicators';
import { TableAction } from '../../tables';
import SchedulesContainer from '../../schedules/SchedulesContainer';

import './ListBackups.scss';

export default class ListBackups extends Component {
  static defaultProps = {
    title : "Backups"
  }

  static propTypes  = {
    currentUniverse: PropTypes.object.isRequired
  }

  componentDidMount() {
    const { currentUniverse: { universeUUID }} = this.props;
    this.props.fetchUniverseBackups(universeUUID);
    this.props.fetchUniverseList();
  }

  componentWillUnmount() {
    this.props.resetUniverseBackups();
  }

  render() {
    const {
      currentCustomer,
      universeBackupList,
      universeTableTypes,
      title,
    } = this.props;
    if (getPromiseState(universeBackupList).isLoading() ||
        getPromiseState(universeBackupList).isInit()) {
      return <YBLoadingCircleIcon size="medium" />;
    }
    const backupInfos = universeBackupList.data.map((b) => {
      const backupInfo = b.backupInfo;
      if (backupInfo.actionType === "CREATE") {
        backupInfo.backupUUID = b.backupUUID;
        backupInfo.status = b.state;
        backupInfo.createTime = b.createTime;
        backupInfo.tableType = universeTableTypes[b.backupInfo.tableUUID];
        // Show action button to restore/delete only when the backup is
        // create and which has completed successfully.
        backupInfo.showActions = (backupInfo.actionType === "CREATE" &&
                                  backupInfo.status === "Completed");
        return backupInfo;
      }
      return null;
    }).filter(Boolean);

    const formatActionButtons = function(item, row) {
      if (row.showActions && isAvailable(currentCustomer.data.features, "universes.backup")) {
        return (
          <DropdownButton className="btn btn-default" title="Actions" id="bg-nested-dropdown" pullRight>
            <TableAction currentRow={row} actionType="restore-backup" />
          </DropdownButton>
        );
      }
    };
    return (
      <div>
        <SchedulesContainer />
        <YBPanelItem
          header={
            <div className="container-title clearfix spacing-top">
              <div className="pull-left">
                <h2 className="task-list-header content-title pull-left">{title}</h2>
              </div>
              <div className="pull-right">
                {isAvailable(currentCustomer.data.features, "universes.backup") &&
                  <div className="backup-action-btn-group">
                    <TableAction className="table-action" btnClass={"btn-orange"}
                                actionType="create-backup" isMenuItem={false} />
                    <TableAction className="table-action" btnClass={"btn-default"}
                                actionType="restore-backup" isMenuItem={false} />
                  </div>
                }
              </div>
            </div>
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
              <TableHeaderColumn dataField="tableType" dataSort
                                columnClassName="no-border name-column" className="no-border">
                Table Type
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
              <TableHeaderColumn dataField={"actions"} columnClassName={"no-border yb-actions-cell"} className={"no-border yb-actions-cell"}
                                dataFormat={formatActionButtons} headerAlign='center' dataAlign='center' >
                Actions
              </TableHeaderColumn>
            </BootstrapTable>
          }
        />
      </div>
    );
  }

}
