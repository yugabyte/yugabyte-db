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
        <div className="pull-left">
          Tables
          <div className="table-type-count">
            <Image src={cassandraLogo} className="table-type-logo"/>
            {numCassandraTables} <span>Cassandra</span>
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
    this.props.fetchUniverseTables(universeUUID);
  }
  render(){
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

    const tablePlacementDummyData = {"read": "-", "write": "-"};

    var isTableMultiAZ = function(item) {
      if (item === true) {
        return <i className="indicator-orange fa fa-check"/>
      } else {
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
          "isMultiAZ": JSON.parse(self.props.universe.currentUniverse.universeDetails.userIntent.isMultiAZ),
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
