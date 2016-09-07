// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import * as moment from 'moment'
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import 'react-bootstrap-table/css/react-bootstrap-table.css';
import { isValidArray, isValidObject } from '../utils/ObjectUtils';
import UniverseModalContainer from '../containers/UniverseModalContainer';
import DeleteUniverseContainer from '../containers/DeleteUniverseContainer';
import DescriptionList from './DescriptionList';

class UniverseDetailsCell extends Component {
  static propTypes = {
    cell: PropTypes.object.isRequired
  };

  render() {
    const { cell } = this.props;
    var universeDetailsItems = Object.keys(cell).map(function(key, index) {
      return {name: key, data: cell[key]}
    });
    return (
      <DescriptionList listItems={universeDetailsItems} />
    )
  }
}

class UniverseButtonGroupCell extends Component {
  render() {
    return (
      <div className="row">
        <div className="col-lg-3">
          <Link to={'/universes/' + this.props.uuid}
            className='universe-button btn
            btn-xs btn-primary '>
            <i className='fa fa-eye'></i>&nbsp;View&nbsp;
          </Link>
        </div>
        <div className="col-lg-3">
          <UniverseModalContainer type={'Edit'} uuid={this.props.uuid} />
        </div>
        <div className="col-lg-3">
          <DeleteUniverseContainer uuid={this.props.uuid} />
        </div>
      </div>
    )
  }
}

export default class UniverseTable extends Component {

  componentWillMount() {
    this.props.fetchUniverseList();
  }

  componentWillUnmount() {
    this.props.resetUniverseList();
  }

  render() {
    var universeDisplay = [];

    function detailStringFormatter(cell, row) {
      return <UniverseDetailsCell cell={cell} />;
    }

    function universeNameTypeFormatter(cell, row) {
      var universeName = cell.split("|")[0];
      var universeCreationDate = cell.split("|")[1];
      return "<div><a>" + universeName + "</a></div><small>Created:<br />"
             + universeCreationDate + "</small>"
    }

    function actionStringFormatter(cell, row) {
      return <UniverseButtonGroupCell uuid={cell} />
    }

    function statusStringFormatter(cell, row){
      if (cell === "failure" ) {
        return "<div class='universe-button btn btn-xs btn-danger'>" +
               "Failure</div>";
      } else if (cell === "success") {
        return "<div class='universe-button btn btn-success btn-xs'>" +
               "Success</div>";
      } else {
        return "<div class='universe-button btn btn-default btn-warning'>" +
               "Pending</div>";
      }
    }

    if (isValidArray(this.props.universe.universeList)) {
      universeDisplay = this.props.universe.universeList.map(function (item, idx) {
        var regionNames = "";
        if (typeof(item.regions) !== "undefined") {
          regionNames = item.regions.map(function(region, idx) {
            return {"idx": idx, "name": region.name};
          });
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

        var universeDetailString= {
          "Provider": providerName,
          "Regions": regionNames.length > 0 ? regionNames: [],
          "Num of Nodes": numNodes,
          "Replication Factor": replicationFactor
        };

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
        var actionString = item.universeUUID;
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

    const tableBodyStyle = {"height": 500,"marginBottom": "1%","paddingBottom": "1%"};

    const selectRowProp = {
      bgColor: "rgb(211,211,211)"
    };

    return (
      <div className="row">
        <BootstrapTable data={universeDisplay}
                        striped={true}
                        hover={true} selectRow={selectRowProp}
                        trClassName="no-border-cell" bodyStyle={tableBodyStyle}>
          <TableHeaderColumn dataField="name"
                             isKey={true}
                             dataFormat={universeNameTypeFormatter} columnClassName="no-border-cell"
                             className="no-border-cell" dataAlign="left" >Universe Name</TableHeaderColumn>
          <TableHeaderColumn dataField="details"
                             dataFormat={detailStringFormatter} columnClassName="no-border-cell"
                             className="no-border-cell" dataAlign="left">Details</TableHeaderColumn>
          <TableHeaderColumn columnClassName="no-border-cell"
                             className="no-border-cell">
          </TableHeaderColumn>
          <TableHeaderColumn dataField="status" dataFormat={statusStringFormatter}
                             columnClassName="no-border-cell" className="no-border-cell">
            Status
          </TableHeaderColumn>
          <TableHeaderColumn dataField="action" dataFormat={actionStringFormatter}
                             columnClassName="no-border-cell table-button-col" className="no-border-cell">
            Actions
          </TableHeaderColumn>
        </BootstrapTable>
      </div>
    )
  }
}
