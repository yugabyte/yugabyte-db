// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { ListGroup, ListGroupItem, Col, Row } from 'react-bootstrap';
import { YBInputField, YBButton } from '../../common/forms/fields';
import { Field } from 'redux-form';
import ProviderConfiguration from '../ConfigProvider/ProviderConfiguration';

class FormDataCell extends Component {
  render() {
    const {cellName, cellLabel} = this.props;
    return (
      <Field name={cellName} label={cellLabel} component={YBInputField} className="data-cell-input"/>
    );
  }
}

export default class AzureProviderConfiguration extends ProviderConfiguration {
  render() {
    return (
      <div className="provider-config-container">
        <h2>Azure</h2>
        <ListGroup>
          <ListGroupItem>
            Configure Microsoft Azure access for YugaWare. See Azure documentation:
            <span className="heading-text"> Compute</span>,
            <span className="heading-text"> Storage</span>,
            <span className="heading-text"> Network</span>.
          </ListGroupItem>
          <ListGroupItem>
            Create an Azure Virtual Network for the YugaByte VMs, and add an Azure Application to launch Yugabyte
            universes.
          </ListGroupItem>
          <ListGroupItem>
            Get the Access Key and Account Name for your Azure Stroage account.
            <div className="detail-text">YugaByte VMs will use this account to attach Azure Virtual Hard Disks (VHDs) to the YugaByte VMs.</div>
          </ListGroupItem>
          <ListGroupItem>
            Enter the details below:
            <form name="AzureConfigForm">
              <Row>
                <Col lg={6}>
                  <div className="form-right-aligned-labels">
                    <FormDataCell cellName={"computeClientId"} cellLabel={"Compute Client ID"}/>
                    <FormDataCell cellName={"computeClientSecret"} cellLabel={"Compute Client Secret"}/>
                    <FormDataCell cellName={"computeTenantId"} cellLabel={"Compute Tenant ID"}/>
                    <FormDataCell cellName={"computeSubscriptionId"} cellLabel={"Compute Subscription ID"}/>
                  </div>
                </Col>
                <Col lg={6}>
                  <div className="form-right-aligned-labels">
                    <FormDataCell cellName={"storageAccessKey"} cellLabel={"Storage Access Key"}/>
                    <FormDataCell cellName={"storageAccountName"} cellLabel={"Storage Account Name"}/>
                  </div>
                </Col>
              </Row>
            </form>
          </ListGroupItem>
        </ListGroup>
        <div className="form-action-button-container">
          <YBButton btnText={"Save"} btnClass={"btn btn-default save-btn pull-right"}/>
          <YBButton btnText={"Cancel"} btnClass={"btn btn-default cancel-btn pull-right"}/>
        </div>
      </div>
    )
  }
}
