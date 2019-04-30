// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { YBPanelItem } from '../../panels';
import { browserHistory} from 'react-router';
import { isNonAvailable } from 'utils/LayoutUtils';

export default class AlertsList extends Component {

  componentWillMount() {
    const { customer: { currentCustomer }} = this.props;
    if (isNonAvailable(currentCustomer.data.features, "alerts.display")) browserHistory.push('/');
  }

  render() {
    const tableBodyContainer = {marginBottom: "1%", paddingBottom: "1%"};
    return (
      <div>
        <h2 className="content-title">Alerts</h2>
        <YBPanelItem
          body={
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
          }
        />
      </div>
    );
  }
}
