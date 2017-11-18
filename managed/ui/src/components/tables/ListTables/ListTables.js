// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Link } from 'react-router';
import { Button, Image, ProgressBar } from 'react-bootstrap';
import cassandraLogo from '../images/cassandra.png';
import redisLogo from '../images/redis.png';
import './ListTables.scss';
import { isNonEmptyArray, isDefinedNotNull } from 'utils/ObjectUtils';
import { CreateTableContainer } from '../../tables';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import 'react-bootstrap-table/css/react-bootstrap-table.css';
import _ from 'lodash';
import {getPromiseState} from '../../../utils/PromiseUtils';

class TableTitle extends Component {
  render() {
    const {onCreateButtonClick, numCassandraTables, numRedisTables} = this.props;
    return (
      <div className="table-container-title clearfix">
        <div className="pull-left">
          <h3>Tables</h3>
          <div className="table-type-count">
            <Image src={cassandraLogo} className="table-type-logo"/>
            {numCassandraTables} <span>Apache Cassandra</span>
          </div>
          <div className="table-type-count">
            <Image src={redisLogo} className="table-type-logo"/>
            {numRedisTables} <span>Redis</span>
          </div>
        </div>
        <div className="pull-right">
          <input type="text" className="table-search-bar" placeholder="Search Tables" />
          <Button bsClass="btn btn-orange" onClick={onCreateButtonClick}>
            Create Table
          </Button>
        </div>
      </div>
    );
  }
}

export default class ListTables extends Component {
  constructor(props) {
    super(props);
    this.state = {'currentView': 'listTables'};
    this.showCreateTable = this.showCreateTable.bind(this);
    this.showListTables = this.showListTables.bind(this);
    this.fetchUniverseTables = this.fetchUniverseTables.bind(this);
  }

  componentWillMount() {
    const universeUUID = this.props.universe.currentUniverse.data.universeUUID;
    const {universe: {universeTasks}} = this.props;
    // Do not send tables query if task type is create, status is pending and target is universe
    if (getPromiseState(universeTasks).isSuccess() && isNonEmptyArray(universeTasks.data[universeUUID])) {
      this.fetchUniverseTables(universeTasks, universeUUID);
    }
  }

  fetchUniverseTables(universeTasks, universeUUID) {
    const createUniverseTask = universeTasks.data[universeUUID].find(function(task){
      return task.target === "Universe" && task.type === "Create" && task.status === "Running" && task.percentComplete < 100;
    });
    if (!isDefinedNotNull(createUniverseTask)) {
      this.props.fetchUniverseTables(universeUUID);
    }
  }

  componentWillReceiveProps(nextProps) {
    const {universe: {universeTasks, currentUniverse}} = nextProps;
    const universeUUID = currentUniverse.data.universeUUID;
    if (getPromiseState(universeTasks).isSuccess() && getPromiseState(this.props.universe.universeTasks).isLoading() && isNonEmptyArray(universeTasks.data[universeUUID])) {
      this.fetchUniverseTables(universeTasks, universeUUID);
    }
  }

  componentWillUnmount() {
    this.props.resetTablesList();
  }

  showCreateTable() {
    this.props.showCreateTable();
  }

  showListTables() {
    this.setState({'currentView': 'listTables'});
  }

  render() {
    const self = this;
    const {tables} = this.props;
    let numCassandraTables = 0;
    let numRedisTables = 0;
    if (isNonEmptyArray(self.props.tables.universeTablesList)) {
      self.props.tables.universeTablesList.forEach(function (item, idx) {
        if (item.tableType === "REDIS_TABLE_TYPE") {
          numRedisTables++;
        } else {
          numCassandraTables++;
        }
      });
    }

    if (tables.currentTableView === "list") {
      return (
        <div>
          <TableTitle numRedisTables={numRedisTables} numCassandraTables={numCassandraTables}
                      onCreateButtonClick={this.showCreateTable}/>
          <ListTableGrid {...this.props}/>
        </div>
      );
    } else if (tables.currentTableView === "create") {
      return (
        <div>
          <CreateTableContainer showListTables={this.showListTables}/>
        </div>
      );
    } else {
      return <span/>;
    }
  }
}

class ListTableGrid extends Component {


  render(){
    const self = this;
    const {universe: {currentUniverse: {data: {universeUUID}}, universeTasks}} = this.props;
    const getTableIcon = function(tableType) {
      return <Image src={tableType === "YQL_TABLE_TYPE" ? cassandraLogo : redisLogo} className="table-type-logo" />;
    };

    const getTableName = function (tableName, data) {
      if (data.status === "success") {
        return <Link to={`/universes/${universeUUID}/tables/${data.tableID}`}>{tableName}</Link>;
      } else {
        return tableName;
      }
    };

    const formatKeySpace = function(cell) {
      return <div className="top-5">{cell}</div>;
    };

    const tablePlacementDummyData = {"read": "-", "write": "-"};

    const formatTableStatus = function(item, row) {
      if (item === "success") {
        return <i className="yb-success-color fa fa-check"/>;
      } else if (item === "pending") {
        return (
          <div>
            <span className="yb-orange">Pending {row.percentComplete} % complete</span>
            <ProgressBar className={"pending-action-progress"} now={row.percentComplete}/>
          </div>
        );
      }
      else {
        return <i className="indicator-orange fa fa-times"/>;
      }
    };

    let listItems = [];
    if (isNonEmptyArray(self.props.tables.universeTablesList)) {
      listItems = self.props.tables.universeTablesList.map(function (item, idx) {
        return {
          "keySpace": item.keySpace,
          "tableID": item.tableUUID,
          "tableType": item.tableType,
          "tableName": item.tableName,
          "status": "success",
          "read": tablePlacementDummyData.read,
          "write": tablePlacementDummyData.write
        };
      });
    }
    const currentUniverseTasks = universeTasks.data[universeUUID];
    if (getPromiseState(universeTasks).isSuccess() && isNonEmptyArray(currentUniverseTasks)) {
      const pendingTableTasks = currentUniverseTasks.find(function(taskItem){
        return taskItem.target === "Table" && taskItem.status === "Running" && taskItem.percentComplete < 100;
      });
      if (pendingTableTasks) {
        // Split title string to extract table name from the title.
        const pendingTableName = pendingTableTasks.title.replace(/.*:\s*/, '');
        if (listItems.findIndex(lItem => lItem.tableName === pendingTableName) === -1) {
          const pendingTableRow = {
            tableID: pendingTableTasks.id,
            tableType: "YQL_TABLE_TYPE",
            tableName: pendingTableName,
            status: "pending",
            percentComplete: pendingTableTasks.percentComplete
          };
          listItems.push(pendingTableRow);
        }
      }
    }
    const sortedListItems = _.sortBy(listItems, "tableName");
    const tableListDisplay = (
      <div className="content-panel content-panel-margin-top">
        <BootstrapTable data={sortedListItems} >
          <TableHeaderColumn dataField="tableID" isKey={true} hidden={true} />
          <TableHeaderColumn dataField={"tableType"} dataFormat={ getTableIcon }
                            columnClassName={"table-type-image-header yb-table-cell"} className={"yb-table-cell"}/>
          <TableHeaderColumn dataField={"keySpace"}
                            columnClassName={"yb-table-cell"} dataFormat={formatKeySpace}>
            Keyspace</TableHeaderColumn>
          <TableHeaderColumn dataField={"tableName"} dataFormat={getTableName}
                            columnClassName={"table-name-label yb-table-cell"} className={"yb-table-cell"}>
            Table Name</TableHeaderColumn>
          <TableHeaderColumn dataField={"status"}
                            columnClassName={"yb-table-cell"} dataFormat={formatTableStatus}>
            Status</TableHeaderColumn>
          <TableHeaderColumn dataField={"read"}
                            columnClassName={"yb-table-cell"} >
            Read</TableHeaderColumn>
          <TableHeaderColumn dataField={"write"}
                            columnClassName={"yb-table-cell"} >
            Write</TableHeaderColumn>
        </BootstrapTable>
      </div>
    );

    return <div>{tableListDisplay}</div>;
  }
}
