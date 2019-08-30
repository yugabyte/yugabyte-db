import React, { Component, Fragment } from 'react';
import { YBPanelItem } from '../panels';
import { Row, Col } from 'react-bootstrap';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { YBButton } from 'components/common/forms/fields';
import { getPromiseState } from 'utils/PromiseUtils';
import moment from 'moment';
import { isNotHidden, isDisabled } from 'utils/LayoutUtils';

import './certificates.scss';
import { AddCertificateFormContainer } from './';

class Certificates extends Component {

  getDateColumn = (key) => (item, row) => {
    if (key in row) {
      return moment.utc(row[key], "YYYY-MM-DD hh:mm:ss a").local().calendar();
    } else {
      return null;
    }
  }

  render() {
    const {
      customer:
        {
          currentCustomer,
          userCertificates
        },
      modal:
        {
          showModal,
          visibleModal
        },
      showAddCertificateModal
      } = this.props;

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
      <div id="page-wrapper">
        <YBPanelItem
          header={
            <Row className="header-row">
              <Col xs={6}>
                <h2 className="content-title">Certificates</h2>

              </Col>
              <Col xs={6} className="universe-table-header-action">
                {isNotHidden(currentCustomer.data.features, "universe.create") &&
                  <YBButton btnClass="universe-button btn btn-lg btn-orange" onClick={showAddCertificateModal}
                    disabled={isDisabled(currentCustomer.data.features, "universe.create")}
                    btnText="Add Certificate" btnIcon="fa fa-plus"/>}
              </Col>
            </Row>
          }
          body={
            <Fragment>
              <BootstrapTable data={certificateArray} className="bs-table-certs" trClassName="tr-cert-name">
                <TableHeaderColumn dataField='name' isKey={true}>Name</TableHeaderColumn>
                <TableHeaderColumn dataField='creationTime' dataFormat={this.getDateColumn('creationTime')}>Creation Time</TableHeaderColumn>
                <TableHeaderColumn dataField='expiryDate' dataFormat={this.getDateColumn('expiryDate')}>Expiration</TableHeaderColumn>
                <TableHeaderColumn dataField='certificate'>Certificate</TableHeaderColumn>
                <TableHeaderColumn dataField='privateKey'>Private Key</TableHeaderColumn>
              </BootstrapTable>
              <AddCertificateFormContainer
                visible={showModal && visibleModal === 'addCertificateModal'}
                onHide={this.props.closeModal}
                fetchCustomerCertificates={this.props.fetchCustomerCertificates}
                />
            </Fragment>
          }
        />
      </div>
    );    
  }
}

export default Certificates;
