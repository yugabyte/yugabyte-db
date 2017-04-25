// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Link } from 'react-router';
import { Button, Image, ProgressBar } from 'react-bootstrap';
import cassandraLogo from '../images/cassandra.png';
import redisLogo from '../images/redis.png';
import './ListTables.scss';
import {isValidArray} from '../../../utils/ObjectUtils';
import { CreateTableContainer } from '../../tables';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import 'react-bootstrap-table/css/react-bootstrap-table.css';
import _ from 'lodash';

class TableTitle extends Component {
  render() {
    const {onCreateButtonClick, numCassandraTables, numRedisTables} = this.props;
    return (
      <div className="table-container-title">
        <div className="pull-left">
          Tables
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
          <Button bsClass="btn bg-orange" onClick={onCreateButtonClick}>
            Create Table
          </Button>
        </div>
      </div>
    )
  }
}

export default class ListTables extends Component {
  constructor(props) {
    super(props);
    this.state = {'currentView': 'listTables'}
    this.showCreateTable = this.showCreateTable.bind(this);
    this.showListTables = this.showListTables.bind(this);
  }

  showCreateTable() {
    this.props.showCreateTable();
  }

  showListTables() {
    this.setState({'currentView': 'listTables'});
  }

  render() {
    var self = this;
    const {tables} = this.props;

    var numCassandraTables = 0;
    var numRedisTables = 0;

    if (isValidArray(self.props.tables.universeTablesList)) {
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
      )
    } else if (tables.currentTableView === "create") {
      return (
        <div>
          <CreateTableContainer showListTables={this.showListTables}/>
        </div>
      )
    } else {
      return <span/>
    }
  }
}

class ListTableGrid extends Component {
  componentWillMount() {
    var universeUUID = this.props.universe.currentUniverse.universeUUID;
    const {universe: {universeTasks}} = this.props;
    // Do not send tables query if task type is create, status is pending and target is universe
    if (!universeTasks || !isValidArray(universeTasks[universeUUID]) || !(universeTasks[universeUUID][0].type === "Create"
      && universeTasks[universeUUID][0].status === "Running" && universeTasks[universeUUID][0].target === "Universe")) {
      this.props.fetchUniverseTables(universeUUID);
    }
  }

  render(){
    var self = this;
    const {universe: {currentUniverse: {universeUUID}, universeTasks}} = this.props;
    var getTableIcon = function(tableType) {
      return <Image src={tableType === "YQL_TABLE_TYPE" ? cassandraLogo : redisLogo} className="table-type-logo" />;
    }

    var getTableName = function (tableName, data) {
      if (data.status === "success") {
        return <Link to={`/universes/${universeUUID}/tables/${data.tableID}`}>{tableName}</Link>;
      } else {
        return tableName
      }
    }

    const tablePlacementDummyData = {"read": "-", "write": "-"};

    var formatTableStatus = function(item, row) {
      if (item === "success") {
        return <i className="yb-success-color fa fa-check"/>
      } else if (item === "pending") {
        return (
          <div>
            <span className="yb-orange">Pending {row.percentComplete} % complete</span>
            <ProgressBar className={"pending-action-progress"} now={row.percentComplete}/>
          </div>
        )
      }
      else {
        return <i className="indicator-orange fa fa-times"/>
      }
    }

    var listItems = [];
    if (isValidArray(self.props.tables.universeTablesList)) {
      listItems = self.props.tables.universeTablesList.map(function (item, idx) {
        return {
          "tableID": item.tableUUID,
          "tableType": item.tableType,
          "tableName": item.tableName,
          "status": "success",
          "read": tablePlacementDummyData.read,
          "write": tablePlacementDummyData.write
        }
      });
    }

    if (universeTasks && universeTasks[universeUUID]) {
      var pendingTableTasks = universeTasks[universeUUID].find(function(taskItem, taskIdx){
        return taskItem.target === "Table" && taskItem.status === "Running" && taskItem.percentComplete < 100
      });
      if (pendingTableTasks) {
        // Split title string to extract table name from the title.
        var pendingTableName = pendingTableTasks.title.replace(/.*:\s*/, '');
        if (listItems.findIndex(lItem => lItem.tableName === pendingTableName) === -1) {
          var pendingTableRow = {
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
    var sortedListItems = _.sortBy(listItems, "tableName");
    var tableListDisplay =
      <BootstrapTable data={sortedListItems} >
        <TableHeaderColumn dataField="tableID" isKey={true} hidden={true} />
        <TableHeaderColumn dataField={"tableType"} dataFormat={ getTableIcon }
                           columnClassName={"table-type-image-header yb-table-cell"} className={"yb-table-cell"}/>
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

    return (<div>{tableListDisplay}</div>)
  }
}
