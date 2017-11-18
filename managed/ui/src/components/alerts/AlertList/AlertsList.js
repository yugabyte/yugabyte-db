// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import {Row, Col} from 'react-bootstrap';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';

export default class AlertsList extends Component {
  render() {
    const tableBodyContainer = {marginBottom: "1%", paddingBottom: "1%"};
    return (
      <div id="page-wrapper">
        <Row className="header-row">
          <Col lg={12}>
            <h2 className="page-topnav-title">Alerts</h2>
            <BootstrapTable data={[]} bodyStyle={tableBodyContainer} pagination={true}>
              <TableHeaderColumn dataField="id" isKey={true} hidden={true}/>
              <TableHeaderColumn dataField="createTime" columnClassName="no-border" className="no-border" dataAlign="left">
                Time
              </TableHeaderColumn>
              <TableHeaderColumn dataField="type" columnClassName="no-border name-column" className="no-border">
                Type
              </TableHeaderColumn>
              <TableHeaderColumn dataField="message" columnClassName="no-border name-column" className="no-border">
                Message
              </TableHeaderColumn>
            </BootstrapTable>
          </Col>
        </Row>
      </div>
    );
  }
}
