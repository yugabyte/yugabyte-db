// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';
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

    function nameFormatter(cell, row) {
      return <span>{row.title.replace(/.*:\s*/, '')}</span>;
    }

    function typeFormatter(cell, row) {
      return <span>{row.type} {row.target}</span>;
    }

    function percentFormatter(cell, row) {
      return <YBFormattedNumber value={cell/100} formattedNumberStyle={"percent"} />;
    }

    function timeFormatter(cell) {
      if (!isValidObject(cell)) {
        return "<span>-</span>";
      } else {
        return (
          <FormattedDate
            value={new Date(cell)}
            year='numeric'
            month='long'
            day='2-digit'
            hour='numeric'
            minute='numeric' />
        );
      }
    }

    function successStringFormatter(cell, row) {
      switch (row.status) {
        case "Success":
          return <span className="yb-success-color"><i className='fa fa-check'/> Completed</span>;
        case "Initializing":
          return <span className="yb-pending-color"><i className='fa fa-spinner fa-spin'/> Initializing</span>;
        case "Running":
          return (
            <span className="yb-pending-color">
              <i className='fa fa-spinner fa-spin'/>Pending ({percentFormatter(row.percentComplete, row)})
          </span>
          );
        case "Failure":
          return <span className="yb-fail-color"><i className='fa fa-warning' /> Failed</span> ;
        default:
          return <span className="yb-fail-color"><i className="fa fa-warning" />Unknown</span>;
      }
    }

    const selectRowProp = {
      bgColor: "rgb(211,211,211)"
    };

    const tableBodyContainer = {marginBottom: "1%", paddingBottom: "1%"};
    return (
      <div id="page-wrapper" className="dashboard-widget-container">
        <h2>{title}</h2>
        <BootstrapTable data={taskList} selectRow={selectRowProp}
                        bodyStyle={tableBodyContainer}
                        pagination={true}>
          <TableHeaderColumn dataField="id" isKey={true} hidden={true}/>
          <TableHeaderColumn dataField="type" dataFormat={typeFormatter}
                             columnClassName="no-border name-column" className="no-border">
            Type
          </TableHeaderColumn>
          <TableHeaderColumn dataField="title" dataFormat={nameFormatter}
                             columnClassName="no-border name-column" className="no-border">
            Name
          </TableHeaderColumn>
          <TableHeaderColumn dataField="percentComplete"
                             columnClassName="no-border name-column" className="no-border"
                             dataFormat={successStringFormatter}>
            Status
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
        </BootstrapTable>
      </div>
    );
  }
}
