import React, { Component } from 'react';
import { YBPanelItem } from '../panels';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { getPromiseState } from 'utils/PromiseUtils';
import moment from 'moment';

import './certificates.scss';

class Certificates extends Component {

  getDateColumn = (key) => (item, row) => {
    if (key in row) {
      return moment(row[key]).calendar();
    } else {
      return null;
    }
  }

  render() {
    const { customer: { userCertificates } } = this.props;
    const certificateArray = getPromiseState(userCertificates).isSuccess() ?
      userCertificates.data.map(cert => {
        return {
          name: cert.label,
          expiryDate: cert.expiryDate,
          certificate: cert.certificate,
          creationTime: cert.startDate,
          privateKey: cert.privateKey,
        };
      }) : [];

    return (
      <YBPanelItem
        header={
          <h2 className="certificates-header content-title">Certificates</h2>
        }
        body={
          <BootstrapTable data={certificateArray} className="bs-table-certs" trClassName="tr-cert-name">
            <TableHeaderColumn dataField='name' isKey={true}>Name</TableHeaderColumn>
            <TableHeaderColumn dataField='creationTime' dataFormat={this.getDateColumn('creationTime')}>Creation Time</TableHeaderColumn>
            <TableHeaderColumn dataField='expiryDate' dataFormat={this.getDateColumn('expiryDate')}>Expiration</TableHeaderColumn>
            <TableHeaderColumn dataField='certificate'>Certificate</TableHeaderColumn>
            <TableHeaderColumn dataField='privateKey'>Private Key</TableHeaderColumn>
          </BootstrapTable>
        }
      />
    );    
  }
}

export default Certificates;
