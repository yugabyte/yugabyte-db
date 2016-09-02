
// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import * as moment from 'moment'
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import 'react-bootstrap-table/css/react-bootstrap-table.css';
import {isValidArray,isValidObject} from '../utils/ObjectUtils';

export default class UniverseTable extends Component {

  componentWillMount() {
    this.props.fetchUniverseList();
  }

  componentWillUnmount() {
    this.props.resetUniverseInfo();
  }

  render() {
    var universeDisplay=[];
    function detailStringFormatter(cell, row) {
      var detailItem = cell.split("|");
      return "<ul><li>Provider: " + detailItem[0] + "</li><li></li>Regions: " +
             detailItem[1] + "</li><li>Nodes: " + detailItem[2] +
             "</li><li>Replication Factor: " + detailItem[3] + "</li></ul>";
    }

    function universeNameTypeFormatter(cell, row) {
      var uniName = cell.split("|")[0];
      var uniDate = cell.split("|")[1];
      return "<a>"+uniName+"</a><br/><small>Created On :" +uniDate+"</small>"
    }

    function actionStringFormatter(cell, row) {
      return cell;
    }

    function statusStringFormatter(cell, row){
      if (cell === "failure" ) {
        return "<div class='btn btn-default btn-danger universeTableButton'>Failure</div>";
      } else if (cell === "success") {
        return "<div class='btn btn-default btn-success universeTableButton'>Success</div>";
      } else {
        return "<div class='btn btn-default btn-warning universeTableButton'>Pending</div>";
      }
    }

    if (isValidArray(this.props.universe.universeList)) {
      universeDisplay = this.props.universe.universeList.map(function (item, idx) {
        var regionNames = "";
        if (typeof(item.regions) !== "undefined") {
          regionNames = item.regions.map(function(region) { return region.name }).join(" ");
        }

        var providerName = "";
        if (isValidObject(item.provider)) {
          providerName = item.provider.name;
        }

        var numNodes = "";
        if (isValidObject(item.universeDetails.numNodes)) {
          numNodes = item.universeDetails.numNodes;
        }

        var replicationFactor = "";
        if (isValidObject(item.universeDetails.userIntent)) {
          replicationFactor = item.universeDetails.userIntent.replicationFactor;
        }

        var universeDetailString = providerName + "|"+regionNames + "|" + numNodes + "|" + replicationFactor;
        var updateProgressStatus = false;
        var updateSuccessStatus = false;
        var status = "";
        if (isValidObject(item.universeDetails.updateInProgress)) {
          updateProgressStatus = item.universeDetails.updateInProgress;
        }
        if (isValidObject(item.universeDetails.updateSucceeded)) {
          updateSuccessStatus = item.universeDetails.updateSucceeded;
        }
        if (!updateProgressStatus && !updateSuccessStatus) {
          status = "failure";
        } else if (updateSuccessStatus) {
          status = "success";
        } else {
          status = "pending";
        }

        var actionString="<a class='btn btn-default btn-primary universeTableButton'>" +
                         "<i class='fa fa-folder'></i>View </a><a href='#'" +
                         "class='btn btn-default btn-info universeTableButton'>" +
                         "<i class='fa fa-pencil'></i> Edit </a><a href='#'" +
                         "class='btn btn-default btn-danger universeTableButton'>" +
                         "<i class='fa fa-trash-o'></i> Delete </a>";

        return {
          id: item.universeUUID,
          name: item.name + "|" + moment.default(item.creationDate).format('MMMM DD, YYYY HH:MM a'),
          details: universeDetailString,
          provider: providerName,
          nodes: numNodes,
          action: actionString,
          status: status
        };
      });
    }

    function universeDetailsPage (row) {
      window.location.href = '/universes/' + row.id;
    }

    const selectRowProp = {
      bgColor: "rgb(211,211,211)",
      mode: "radio",
      clickToSelect: true,
      onSelect: universeDetailsPage,
      hideSelectColumn: true
    };

    return (
      <div className="row">
        <BootstrapTable data={universeDisplay}
                        striped={true}
                        hover={true}
                        selectRow={selectRowProp} trClassName="no-border-cell" height="600"
                        tableStyle="table table-striped projects">
          <TableHeaderColumn dataField="name"
                             dataSort={true}
                             isKey={true}
                             dataFormat={universeNameTypeFormatter} columnClassName="no-border-cell"
                             className="no-border-cell">Universe Name</TableHeaderColumn>
          <TableHeaderColumn dataField="details"
                             dataAlign="start"
                             dataFormat={detailStringFormatter} columnClassName="no-border-cell"
                             className="no-border-cell">Details</TableHeaderColumn>
          <TableHeaderColumn columnClassName="no-border-cell"
                             className="no-border-cell">
          </TableHeaderColumn>
          <TableHeaderColumn dataField="status" dataFormat={statusStringFormatter}
                             columnClassName="no-border-cell" className="no-border-cell">
            Status
          </TableHeaderColumn>
          <TableHeaderColumn dataField="action" dataFormat={actionStringFormatter}
                             columnClassName="no-border-cell" className="no-border-cell">
            Actions
          </TableHeaderColumn>
        </BootstrapTable>
      </div>
    )
  }
}
