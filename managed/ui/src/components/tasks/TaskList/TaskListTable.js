// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { isValidObject } from '../../../utils/ObjectUtils';
import { FormattedDate } from 'react-intl';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import './AlertsList.css'
import {YBFormattedNumber} from '../../common/descriptors';

export default class TaskListTable extends Component {
  static defaultProps = {
    title : "Tasks"
  }
  static propTypes  = {
    taskList : PropTypes.array.isRequired
  }
  render() {
    const {taskList, title} = this.props;

    function percentFormatter(cell, row) {
      return <YBFormattedNumber value={cell/100} formattedNumberStyle={"percent"} />;
    }

    function timeFormatter(cell) {
      if (!isValidObject(cell)) {
        return "<span>-</span>";
      } else {
        return <FormattedDate value={new Date(cell)}
                              year='numeric'
                              month='long'
                              day='2-digit'
                              hour='numeric'
                              minute='numeric'/>
      }
    }
    function successStringFormatter(cell, row) {
      switch (cell) {
        case "Success" :
          return <span><i className='fa fa-check'/> Succeeded</span>;
        case "Initializing" :
          return <span><i className='fa fa-spinner fa-spin'/> Initializing</span>;
        case "Running" :
          return <span><i className='fa fa-spinner fa-spin'/> Pending</span>;
        case "Failure" :
          return <span><i className='fa fa-times' /> Failed</span> ;
        default :
          return <span><i className="fa fa-exclamation" />Unknown</span>;
      }
    }

    const selectRowProp = {
      bgColor: "rgb(211,211,211)"
    };

    const tableBodyContainer = {"marginBottom": "1%", "paddingBottom": "1%"}
    return (
      <div id="page-wrapper" className="dashboard-widget-container">
        <h4>{title}</h4>
          <BootstrapTable data={taskList} selectRow={selectRowProp}
                          bodyStyle={tableBodyContainer}
                          pagination={true}>
            <TableHeaderColumn dataField="id" isKey={true} hidden={true}/>
            <TableHeaderColumn dataField="title"
                               columnClassName="no-border name-column" className="no-border">
              Title
            </TableHeaderColumn>
            <TableHeaderColumn dataField="percentComplete" dataFormat={percentFormatter}
                               columnClassName="no-border name-column-sm" className="no-border name-column-sm">
              Progress
            </TableHeaderColumn>
            <TableHeaderColumn dataField="createTime" dataFormat={timeFormatter}
                               columnClassName="no-border " className="no-border"
                               dataAlign="left">
              Start Time
            </TableHeaderColumn>
            <TableHeaderColumn dataField="completionTime" dataFormat={timeFormatter}
                               columnClassName="no-border name-column" className="no-border">
              End Time
            </TableHeaderColumn>
            <TableHeaderColumn dataField="type"
                               columnClassName="no-border name-column" className="no-border">
              Type
            </TableHeaderColumn>
            <TableHeaderColumn dataField="status"
                               columnClassName="no-border name-column" className="no-border"
                               dataFormat={successStringFormatter}>
              Status
            </TableHeaderColumn>
          </BootstrapTable>
      </div>

    )
  }
}
