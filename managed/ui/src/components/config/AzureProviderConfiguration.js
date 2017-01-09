// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { ListGroup, ListGroupItem, Col, Row } from 'react-bootstrap';
import { YBInputField, YBButton } from '../common/forms/fields';
import { Field } from 'redux-form';
import ProviderConfiguration from './ProviderConfiguration';

class FormDataCell extends Component {
  render() {
    const {cellName, cellLabel} = this.props;
    return (
      <Col lg={6}>
        <Field name={cellName} label={cellLabel} component={YBInputField} className="data-cell-input"/>
      </Col>
    )
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
                 <FormDataCell cellName={"computeClientId"} cellLabel={"Compute Client ID:"}/>
                 <FormDataCell cellName={"storageAccessKey"} cellLabel={"Storage Access Key:"}/>
              </Row>
              <Row>
                <FormDataCell cellName={"computeClientSecret"} cellLabel={"Compute Client Secret:"}/>
                <FormDataCell cellName={"storageAccountName"} cellLabel={"Storage Account Name:"}/>
              </Row>
              <Row>
                <FormDataCell cellName={"computeTenantId"} cellLabel={"Compute Tenant ID:"}/>
              </Row>
              <Row>
                <FormDataCell cellName={"computeSubscriptionId"} cellLabel={"Compute Subscription ID:"}/>
              </Row>
            </form>
          </ListGroupItem>
        </ListGroup>
        <Row>
          <Col lg={4} lgOffset={8}>
            <YBButton btnText={"Cancel"} btnClass={"btn btn-default cancel-btn"}/>
            <YBButton btnText={"Save"} btnClass={"btn btn-default save-btn"}/>
          </Col>
        </Row>
      </div>
    )
  }
}
