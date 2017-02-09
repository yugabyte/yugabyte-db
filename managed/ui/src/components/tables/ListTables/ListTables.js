// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Link } from 'react-router';
import { Button, Image } from 'react-bootstrap';
import cassandraLogo from '../images/cassandra.png';
import redisLogo from '../images/redis.png';
import './ListTables.scss';
import {isValidArray} from '../../../utils/ObjectUtils';
import { CreateTableContainer } from '../../tables';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import 'react-bootstrap-table/css/react-bootstrap-table.css';


class TableTitle extends Component {
  render() {
    const {onCreateButtonClick, numCassandraTables, numRedisTables} = this.props;
    return (
      <div className="table-container-title">
        Tables
        &nbsp; &nbsp;
        <Image src={redisLogo} className="table-type-logo"/>&nbsp;
        {numRedisTables} <span>Redis</span> &nbsp; &nbsp; 
        <Image src={cassandraLogo} className="table-type-logo"/>&nbsp; 
        {numCassandraTables} <span>Cassandra</span>
        <div>
          <input type="text" className="table-search-bar "/>
          <Button onClick={onCreateButtonClick}>
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

  componentWillMount() {
    var universeUUID = this.props.universe.currentUniverse.universeUUID;
    this.props.fetchUniverseTables(universeUUID);
  }

  showCreateTable() {
    this.setState({'currentView': 'createTable'});
  }

  showListTables() {
    this.setState({'currentView': 'listTables'});
  }

  render() {
    var self = this;
    const {universe: {currentUniverse}} = this.props;
    var getTableIcon = function(tableType) {
      if (tableType === "YQL_TABLE_TYPE") {
        return <Image src={cassandraLogo} className="table-type-logo"/>;
      } else {
          return <Image src={redisLogo} className="table-type-logo"/>;
        }
    }

    var getTableName = function (tableName, data) {
      return <Link to={`/universes/${currentUniverse.universeUUID}/tables/${data.tableID}`}>{tableName}</Link>;
    }

    const tablePlacementDummyData = { "asyncReplica": ["-"],
                                      "remoteCache": ["-"],
                                      "read": "-", "write": "-"};

    var isTableMultiAZ = function(item) {
      if (item === true) {
        return <i className="indicator-orange fa fa-check"/>
      } else {
        return <i className="indicator-orange fa fa-times"/>
      }
    }

    var numCassandraTables = 0;
    var numRedisTables = 0;
    var listItems = [];
    if (isValidArray(self.props.tables.universeTablesList)) {
      listItems = self.props.tables.universeTablesList.map(function (item, idx) {
        if (item.tableType === "redis") {
          numRedisTables++;
        } else {
          numCassandraTables++;
        }
        return {
          "tableID": item.tableUUID,
          "tableType": item.tableType,
          "tableName": item.tableName,
          "isMultiAZ": JSON.parse(self.props.universe.currentUniverse.universeDetails.userIntent.isMultiAZ),
          "asyncReplica": tablePlacementDummyData.asyncReplica,
          "remoteCache": tablePlacementDummyData.remoteCache,
          "read": tablePlacementDummyData.read,
          "write": tablePlacementDummyData.write
        }
      });
    }

    var tableListDisplay =
      <BootstrapTable data={listItems} >
        <TableHeaderColumn dataField="tableID" isKey={true} hidden={true} />
        <TableHeaderColumn dataField={"tableType"} dataFormat={ getTableIcon }
                           columnClassName={"table-type-image-header yb-table-cell"} className={"yb-table-cell"}/>
        <TableHeaderColumn dataField={"tableName"} dataFormat={getTableName}
                           columnClassName={"table-name-label yb-table-cell"} className={"yb-table-cell"}>
          Table Name</TableHeaderColumn>
        <TableHeaderColumn dataField={"isMultiAZ"}
                           columnClassName={"yb-table-cell"} dataFormat={isTableMultiAZ}>
          Multi AZ</TableHeaderColumn>
        <TableHeaderColumn dataField={"asyncReplica"}
                           columnClassName={"yb-table-cell"} >
          Async Replica</TableHeaderColumn>
        <TableHeaderColumn dataField={"remoteCache"}
                           columnClassName={"yb-table-cell"} >
          Remote Cache</TableHeaderColumn>
        <TableHeaderColumn dataField={"read"}
                           columnClassName={"yb-table-cell"} >
          Read</TableHeaderColumn>
        <TableHeaderColumn dataField={"write"}
                           columnClassName={"yb-table-cell"} >
          Write</TableHeaderColumn>
      </BootstrapTable>

    if (self.state.currentView === "listTables") {
      return (
        <div>
          <TableTitle numRedisTables={numRedisTables} numCassandraTables={numCassandraTables}
                      onCreateButtonClick={this.showCreateTable}/>
          {tableListDisplay}
        </div>
      )
    } else if (self.state.currentView === "createTable") {
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
