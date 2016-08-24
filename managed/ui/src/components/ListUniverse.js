// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import * as moment from 'moment'
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import 'react-bootstrap-table/css/react-bootstrap-table.css';
export default class ListUniverse extends Component {

  constructor(props) {
    super(props);
  }

  render() {
    var universeDisplay = [];
    if(typeof this.props.customer.universes !== "undefined") {
      universeDisplay = this.props.customer.universes.map(function (item, idx) {
        return {
          id: item.universeUUID,
          name: item.name,
          created: moment.default(item.creationDate).format(),
          masters: item.masters.length,
          nodes: item.masters.length, //Placeholder values , since nodes is not present in the current response
          status: "active"
        };
      });
    }

    const selectRowProp = {
      bgColor: "rgb(211,211,211)"
    };
    return (
      <div className="row">
        <BootstrapTable data={universeDisplay} striped={true} hover={true} selectRow={selectRowProp}>
          <TableHeaderColumn dataField="name" dataSort={true} isKey={true}>Universe Name</TableHeaderColumn>
          <TableHeaderColumn dataField="created" dataAlign="left">Created On</TableHeaderColumn>
          <TableHeaderColumn dataField="masters" >Masters</TableHeaderColumn>
          <TableHeaderColumn dataField="nodes" >Replication Factor</TableHeaderColumn>
          <TableHeaderColumn dataField="nodes" >Nodes</TableHeaderColumn>
          <TableHeaderColumn dataField="status" >Status</TableHeaderColumn>
        </BootstrapTable>
      </div>
    )
  }
}
