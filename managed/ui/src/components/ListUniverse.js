// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import * as moment from 'moment'
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
export default class ListUniverse extends Component {

  constructor(props) {
    super(props);
    this.showUniverseDialog = this.showUniverseDialog.bind(this);
  }

  showUniverseDialog (){
      //TODO show Selected Universe Page
  }

  render(){
    const universeDisplay = [];
    if(typeof this.props.customer.universes !== "undefined") {
      this.props.customer.universes.map(function (item, idx) {
        universeDisplay.push({
          id: item.universeUUID,
          name: item.name,
          created:moment.default(item.creationDate).format(),
          masters:item.masters.length,
          nodes:item.masters.length, //Placeholder values , since nodes is not present in the current response
          status:"active"
        });
      });
    }

    const selectRowProp = {
      bgColor: "rgb(211,211,211)",   //selected row background color
      onSelect:this.showUniverseDialog()
    };

    return (
      <div className="universe-grid-container">
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
