// Copyright (c) YugaByte, Inc.

import { Component, Fragment } from 'react';
import { Link } from 'react-router';
import { Image, ProgressBar, ButtonGroup, DropdownButton } from 'react-bootstrap';
import { toast } from 'react-toastify';
import { Typography } from '@material-ui/core';

import tableIcon from '../images/table.png';
import { isNonEmptyArray } from '../../../utils/ObjectUtils';
import { TableAction } from '../../tables';
import { YBPanelItem } from '../../panels';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import 'react-bootstrap-table/css/react-bootstrap-table.css';
import _ from 'lodash';
import { getPromiseState } from '../../../utils/PromiseUtils';
import { YBBanner, YBBannerVariant, YBResourceCount } from '../../common/descriptors';
import { isDisabled, isNotHidden } from '../../../utils/LayoutUtils';
import { formatSchemaName } from '../../../utils/Formatters';
import { YBButtonLink } from '../../common/forms/fields';
import { YBButton } from '../../../redesign/components';
import {
  getTableName,
  getTableUuid,
  getIsColocatedChildTable,
  getIsColocatedParentTable
} from '../../../utils/tableUtils';
import { SchemaChangeModeInfoModal } from '../../xcluster/sharedComponents/SchemaChangeInfoModal';
import { getPrimaryCluster } from '../../../utils/universeUtilsTyped';

import './ListTables.scss';

class TableTitle extends Component {
  render() {
    const {
      numCassandraTables,
      numRedisTables,
      numPostgresTables,
      universe,
      tables,
      fetchUniverseTables
    } = this.props;
    const currentUniverseUUID = universe?.currentUniverse?.data?.universeUUID;
    const fetchCurrentUniverseTables = (currentUniverseUUID) => {
      fetchUniverseTables(currentUniverseUUID);
      if (tables?.error?.message) {
        toast.error('Refresh failed, try again', {
          // Adding toastId helps prevent multiple duplicate toasts that appears
          // on screen when user presses refresh button multiple times
          toastId: 'table-fetch-failure'
        });
      }
    };

    return (
      <div className="table-container-title clearfix">
        <div className="pull-left">
          <h2>Tables</h2>
          <div className="table-type-count">
            <Image src={tableIcon} className="table-type-logo" />
            <YBResourceCount kind="YSQL" size={numPostgresTables} />
          </div>
          <div className="table-type-count">
            <Image src={tableIcon} className="table-type-logo" />
            <YBResourceCount kind="YCQL" size={numCassandraTables} />
          </div>
          <div className="table-type-count">
            <Image src={tableIcon} className="table-type-logo" />
            <YBResourceCount kind="YEDIS" size={numRedisTables} />
          </div>
        </div>
        <div className="pull-right">
          <YBButtonLink
            btnIcon="fa fa-refresh"
            btnClass="btn btn-default refresh-table-list-btn"
            onClick={() => fetchCurrentUniverseTables(currentUniverseUUID)}
          />
        </div>
      </div>
    );
  }
}

export default class ListTables extends Component {
  constructor(props) {
    super(props);
    this.state = { currentView: 'listTables', isSchemaChangeInfoModalOpen: false };
  }

  showListTables = () => {
    this.setState({ currentView: 'listTables' });
  };

  openSchemaChangeInfoModal = () => {
    this.setState({ isSchemaChangeInfoModalOpen: true });
  };
  closeSchemaChangeInfoModal = () => {
    this.setState({ isSchemaChangeInfoModalOpen: false });
  };

  render() {
    const self = this;
    const { tables } = this.props;
    let numCassandraTables = 0;
    let numRedisTables = 0;
    let numPostgresTables = 0;
    if (isNonEmptyArray(self.props.tables.universeTablesList)) {
      self.props.tables.universeTablesList.forEach(function (item, idx) {
        if (item.tableType === 'REDIS_TABLE_TYPE') {
          numRedisTables++;
        } else if (item.tableType === 'YQL_TABLE_TYPE') {
          numCassandraTables++;
        } else {
          numPostgresTables++;
        }
      });
    }

    const currentUniverse = this.props.universe.currentUniverse.data;
    const hasTablesInXClusterReplication = !!(
      currentUniverse.drConfigUuidsAsSource.length ||
      currentUniverse.drConfigUuidsAsTarget.length ||
      currentUniverse.universeDetails.xclusterInfo.sourceXClusterConfigs.length ||
      currentUniverse.universeDetails.xclusterInfo.sourceXClusterConfigs.length
    );

    const clusters = currentUniverse?.universeDetails.clusters;
    const currentUniverseVersion = clusters
      ? getPrimaryCluster(clusters)?.userIntent.ybSoftwareVersion ?? ''
      : '';

    if (tables.currentTableView === 'list') {
      return (
        <>
          {hasTablesInXClusterReplication && (
            <>
              <YBBanner variant={YBBannerVariant.INFO} isFeatureBanner={true}>
                <div className="universe-tables-bannerContainer">
                  <Typography variant="body2">
                    <b>One or more tables in this universe is involved with xCluster.</b> Before
                    making schema changes to tables in replication, review the steps for making
                    manual schema changes.
                  </Typography>
                  <div className="universe-tables-bannerActionButtonContainer">
                    <YBButton
                      variant="secondary"
                      size="large"
                      onClick={this.openSchemaChangeInfoModal}
                    >
                      Learn More
                    </YBButton>
                  </div>
                </div>
              </YBBanner>
              {this.state.isSchemaChangeInfoModalOpen && (
                <SchemaChangeModeInfoModal
                  currentUniverseVersion={currentUniverseVersion}
                  isDrInterface={true}
                  isConfigInterface={false}
                  modalProps={{
                    open: this.state.isSchemaChangeInfoModalOpen,
                    onClose: this.closeSchemaChangeInfoModal
                  }}
                />
              )}
            </>
          )}
          <YBPanelItem
            header={
              <TableTitle
                numRedisTables={numRedisTables}
                numCassandraTables={numCassandraTables}
                numPostgresTables={numPostgresTables}
                {...this.props}
              />
            }
            body={<ListTableGrid {...this.props} />}
          />
        </>
      );
    } else {
      return <span />;
    }
  }
}

class ListTableGrid extends Component {
  render() {
    const self = this;
    const {
      universe: { universeTasks },
      customer: { currentCustomer }
    } = this.props;
    const currentUniverse = this.props.universe.currentUniverse.data;
    const universePaused = this.props.universe.currentUniverse?.data?.universeDetails
      ?.universePaused;
    const getTableIcon = function (tableType) {
      if (tableType === 'YQL_TABLE_TYPE') {
        return 'YCQL';
      } else if (tableType === 'REDIS_TABLE_TYPE') {
        return 'YEDIS';
      } else {
        return 'YSQL';
      }
    };

    const formatTableName = function (tableName, data) {
      if (data?.status === 'success' && !data?.isParentTable) {
        return (
          <Link
            title={tableName}
            to={`/universes/${currentUniverse.universeUUID}/tables/${data.tableID}`}
          >
            {tableName}
          </Link>
        );
      } else {
        return tableName;
      }
    };

    const formatKeySpace = function (cell) {
      return <div>{cell}</div>;
    };
    const actions_disabled =
      isDisabled(currentCustomer.data.features, 'universes.backup') ||
      currentUniverse.universeDetails.updateInProgress;
    const disableManualBackup = currentUniverse?.universeConfig?.takeBackups === 'true';
    const formatActionButtons = function (item, row, disabled) {
      if (!row.isIndexTable) {
        const actions = [];
        if (getTableIcon(row.tableType) === 'YCQL') {
          actions.push([
            <TableAction
              key={`${row.tableName}-import-btn`}
              currentRow={row}
              actionType="import"
              disabled={actions_disabled}
              universeUUID={currentUniverse.universeUUID}
            />
          ]);
        }
        if (actions.length === 0) return null;
        return (
          <ButtonGroup>
            <DropdownButton
              className="btn btn-default"
              title="Actions"
              id="bg-nested-dropdown"
              pullRight
            >
              {actions}
            </DropdownButton>
          </ButtonGroup>
        );
      }
    };

    const formatTableStatus = function (item, row) {
      if (item === 'success') {
        return <i className="yb-success-color fa fa-check" />;
      } else if (item === 'pending') {
        return (
          <div>
            <span className="yb-orange">Pending {row.percentComplete} % complete</span>
            <ProgressBar className={'pending-action-progress'} now={row.percentComplete} />
          </div>
        );
      } else {
        return <i className="indicator-orange fa fa-times" />;
      }
    };

    const formatBytes = function (item, row) {
      if (Number.isInteger(item)) {
        const bytes = item;
        const sizes = ['B', 'KB', 'MB', 'GB', 'TB', 'PB', 'EB'];
        const k = 1024;
        if (bytes <= 0) {
          return bytes + ' ' + sizes[0];
        }

        const sizeIndex = Math.floor(Math.log(bytes) / Math.log(k));
        return parseFloat((bytes / Math.pow(k, sizeIndex)).toFixed(2)) + ' ' + sizes[sizeIndex];
      } else {
        return '-';
      }
    };

    let listItems = [];
    if (isNonEmptyArray(self.props.tables.universeTablesList)) {
      listItems = self.props.tables.universeTablesList.map((ybTable) => ({
        keySpace: ybTable.keySpace,
        tableID: getTableUuid(ybTable),
        pgSchemaName: ybTable.pgSchemaName,
        tableType: ybTable.tableType,
        tableName: getTableName(ybTable),
        status: 'success',
        isIndexTable: ybTable.isIndexTable,
        sizeBytes: getIsColocatedChildTable(ybTable) ? '-' : ybTable.sizeBytes,
        walSizeBytes: getIsColocatedChildTable(ybTable) ? '-' : ybTable.walSizeBytes,
        isParentTable: getIsColocatedParentTable(ybTable)
      }));
    }
    const currentUniverseTasks = universeTasks.data[currentUniverse.universeUUID];
    if (getPromiseState(universeTasks).isSuccess() && isNonEmptyArray(currentUniverseTasks)) {
      const pendingTableTasks = currentUniverseTasks.find(function (taskItem) {
        return (
          taskItem.target === 'Table' &&
          taskItem.status === 'Running' &&
          taskItem.percentComplete < 100
        );
      });
      if (pendingTableTasks) {
        // Split title string to extract table name from the title.
        const pendingTableName = pendingTableTasks.title.replace(/.*:\s*/, '');
        if (listItems.findIndex((lItem) => lItem.tableName === pendingTableName) === -1) {
          const pendingTableRow = {
            tableID: pendingTableTasks.id,
            pgSchemaName: pendingTableTasks.pgSchemaName,
            tableType: 'YQL_TABLE_TYPE',
            tableName: pendingTableName,
            status: 'pending',
            actions: false,
            percentComplete: pendingTableTasks.percentComplete
          };
          listItems.push(pendingTableRow);
        }
      }
    }
    const sortedListItems = _.sortBy(listItems, 'tableName');
    const tableListDisplay = (
      <BootstrapTable data={sortedListItems} pagination search className="middle-aligned-table">
        <TableHeaderColumn dataField="tableID" isKey={true} hidden={true} />
        <TableHeaderColumn
          dataField={'tableName'}
          dataFormat={formatTableName}
          width="15%"
          columnClassName={'table-name-label yb-table-cell'}
          className={'yb-table-cell'}
          dataSort
        >
          Table Name
        </TableHeaderColumn>
        <TableHeaderColumn
          dataField="pgSchemaName"
          width="15%"
          dataFormat={(cell, row) => formatSchemaName(row.tableType, cell)}
        >
          Schema Name
        </TableHeaderColumn>
        <TableHeaderColumn
          dataField={'tableType'}
          dataFormat={getTableIcon}
          width="15%"
          columnClassName={'table-type-image-header yb-table-cell'}
          className={'yb-table-cell'}
          dataSort
        >
          Table Type
        </TableHeaderColumn>
        <TableHeaderColumn
          dataField={'keySpace'}
          width="15%"
          columnClassName={'yb-table-cell'}
          dataFormat={formatKeySpace}
          dataSort
        >
          Keyspace
        </TableHeaderColumn>
        <TableHeaderColumn
          dataField={'status'}
          width="15%"
          columnClassName={'yb-table-cell'}
          dataFormat={formatTableStatus}
        >
          Status
        </TableHeaderColumn>
        <TableHeaderColumn
          dataField={'sizeBytes'}
          width="15%"
          columnClassName={'yb-table-cell'}
          dataFormat={formatBytes}
          dataSort
        >
          SST Size
        </TableHeaderColumn>
        <TableHeaderColumn
          dataField={'walSizeBytes'}
          width="15%"
          columnClassName={'yb-table-cell'}
          dataFormat={formatBytes}
          dataSort
        >
          WAL Size
        </TableHeaderColumn>
        {!universePaused &&
          isNotHidden(currentCustomer.data.features, 'universes.backup') &&
          sortedListItems.filter((t) => t.tableType === 'YQL_TABLE_TYPE').length > 0 && (
            <TableHeaderColumn
              dataField={'actions'}
              columnClassName={'yb-actions-cell'}
              width="10%"
              dataFormat={formatActionButtons}
            >
              Actions
            </TableHeaderColumn>
          )}
      </BootstrapTable>
    );
    return <Fragment>{tableListDisplay}</Fragment>;
  }
}
