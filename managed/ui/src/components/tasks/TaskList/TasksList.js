// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { FormattedDate } from 'react-intl';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { isValidObject } from '../../../utils/ObjectUtils';
import { YBPanelItem } from '../../panels';
import './AlertsList.css'
import {YBFormattedNumber} from '../../common/descriptors';

export default class TasksList extends Component {

  componentWillMount() {
    this.props.fetchUniverseTasks();
  }

  render() {

    function percentFormatter(cell, row) {
      return <YBFormattedNumber value={cell/100} formattedNumberStyle={"percent"} />;
    }

    function timeFormatter(cell, row) {
      if (!isValidObject(cell)) {
        return "<span>-</span>";
      } else {
        return <FormattedDate value={new Date(cell)}
                              year='numeric'
                              month='long'
                              day='2-digit'
                              hour='numeric'
                              minute='numeric'
               />
      }
    }
    function successStringFormatter(cell, row) {
      if(cell === true ){
        return <i className='fa fa-check'> Succeeded </i>;
      } else {
        return <i className='fa fa-times' > Failed </i>;
      }
    }
    const {universe: {universeTasks}} = this.props;

    var alertsDisplay = [];

    if (isValidObject(universeTasks)) {
      Object.keys(universeTasks).forEach(function (key, idx) {
        alertsDisplay = [].concat(alertsDisplay, universeTasks[key]);
      });
    }

    const selectRowProp = {
      bgColor: "rgb(211,211,211)"
    };

    const tableBodyContainer = {"marginBottom": "1%", "paddingBottom": "1%"}
      return (
        <div id="page-wrapper" className="dashboard-widget-container">
        <YBPanelItem name="Tasks">
          <BootstrapTable data={alertsDisplay} selectRow={selectRowProp}
                          bodyStyle={tableBodyContainer}
                          pagination={true}>
            <TableHeaderColumn dataField="id" isKey={true} hidden={true}/>
            <TableHeaderColumn dataField="title"
                               columnClassName="no-border-cell name-column" className="no-border-cell">
               Title
            </TableHeaderColumn>
            <TableHeaderColumn dataField="percentComplete" dataFormat={percentFormatter}
                               columnClassName="no-border-cell name-column-sm" className="no-border-cell name-column-sm">
              Progress
            </TableHeaderColumn>
            <TableHeaderColumn dataField="createTime" dataFormat={timeFormatter}
                               columnClassName="no-border-cell " className="no-border-cell"
                               dataAlign="left">
              Start Time
            </TableHeaderColumn>
            <TableHeaderColumn dataField="completionTime" dataFormat={timeFormatter}
                               columnClassName="no-border-cell name-column" className="no-border-cell">
               End Time
            </TableHeaderColumn>
            <TableHeaderColumn dataField="type"
                               columnClassName="no-border-cell name-column" className="no-border-cell">
               Type
            </TableHeaderColumn>
            <TableHeaderColumn dataField="success"
                               columnClassName="no-border-cell name-column" className="no-border-cell"
                               dataFormat={successStringFormatter}>
               Status
            </TableHeaderColumn>
          </BootstrapTable>
        </YBPanelItem>
          </div>

      )
    }
}
