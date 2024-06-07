// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { YBPanelItem } from '../../panels';
import { showOrRedirect } from '../../../utils/LayoutUtils';
import { alertSeverityFormatter } from '../../../utils/TableFormatters';

export default class AlertsList extends Component {
  componentDidMount() {
    this.props.getAlertsList();
  }

  render() {
    const {
      customer: { currentCustomer, alerts }
    } = this.props;
    showOrRedirect(currentCustomer.data.features, 'menu.alerts');

    const tableBodyContainer = { marginBottom: '1%', paddingBottom: '1%' };

    const getAlertName = function (cell, row) {
      return row.labels
        .filter((label) => label.name === 'definition_name')
        .map((label) => label.value)
        .shift();
    };

    return (
      <div>
        <h2 className="content-title">Alerts</h2>
        <YBPanelItem
          body={
            <BootstrapTable
              data={alerts.alertsList}
              bodyStyle={tableBodyContainer}
              pagination={true}
            >
              <TableHeaderColumn dataField="uuid" isKey={true} hidden={true} />
              <TableHeaderColumn
                dataField="createTime"
                columnClassName="no-border"
                className="no-border"
                dataAlign="left"
                width={'20%'}
              >
                Time
              </TableHeaderColumn>
              <TableHeaderColumn
                dataField="severity"
                columnClassName="no-border name-column"
                className="no-border"
                dataFormat={alertSeverityFormatter}
                width={'10%'}
              >
                Type
              </TableHeaderColumn>
              <TableHeaderColumn
                dataField=""
                dataFormat={getAlertName}
                columnClassName="no-border name-column"
                className="no-border"
                width={'20%'}
              >
                Alert Name
              </TableHeaderColumn>
              <TableHeaderColumn
                dataField="message"
                columnClassName="no-border name-column"
                className="no-border"
                width={'50%'}
                tdStyle={{ whiteSpace: 'normal' }}
              >
                Message
              </TableHeaderColumn>
            </BootstrapTable>
          }
        />
      </div>
    );
  }
}
