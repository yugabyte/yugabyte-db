// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { withRouter } from 'react-router';
import { Formik, Field } from 'formik';
import { Row, Col } from 'react-bootstrap';
import { YBButton } from '../../../common/forms/fields';
import {  YBFormInput } from '../../../common/forms/fields';
import { isEnabled } from '../../../../utils/LayoutUtils';

class AzureProviderConfiguration extends Component {
  createProviderConfig = (vals, submitting) => {
    const config = {}
    config["AZURE_CLIENT_ID"] = vals.azureClientId
    config["AZURE_CLIENT_SECRET"] = vals.azureClientSecret
    config["AZURE_TENANT_ID"] = vals.azureTenantId
    config["AZURE_SUBSCRIPTION_ID"] = vals.azureSubscriptionId
    config["AZURE_RG"] = vals.azureResourceGroup
    const perRegionMetadata = {"westus2": {}}
    vals["perRegionMetadata"] = perRegionMetadata
    this.props.createAzureProvider(vals.accountName, config, vals)
  }
  render() {
    const { customer: {currentCustomer} } = this.props;
    if (isEnabled(currentCustomer.data.features, "universes.providers.azure", "disabled")) {
      return (
        <div className="provider-config-container">
          <Formik
            onSubmit={(values, { setSubmitting }) => {
              const payload = {
                ...values,
              };
              this.createProviderConfig(payload, setSubmitting);
            }}
            render = {props => (
              <form name="kubernetesConfigForm"
                      onSubmit={props.handleSubmit}>
              <div className="editor-container">
                <Col lg={8}>
                  <Row className="config-provider-row">
                    <Col lg={7}>
                      <Field name="accountName" placeholder="Azure Config name"
                            component={YBFormInput}
                            className={"azure-provider-input-field"}/>
                      <Field name="azureClientId" placeholder="Azure Client Id"
                            component={YBFormInput}
                            className={"azure-provider-input-field"}/>
                      <Field name="azureClientSecret" placeholder="Azure Client Secret"
                            component={YBFormInput}
                            className={"azure-provider-input-field"}/>
                      <Field name="azureTenantId" placeholder="Azure Tenant Id"
                            component={YBFormInput}
                            className={"azure-provider-input-field"}/>
                      <Field name="azureSubscriptionId" placeholder="Azure Subscription Id"
                            component={YBFormInput}
                            className={"azure-provider-input-field"}/>
                      <Field name="azureResourceGroup" placeholder="Azure Resource Group"
                            component={YBFormInput}
                            className={"azure-provider-input-field"}/>
                    </Col>
                  </Row>
                </Col>
              </div>
              <div className="form-action-button-container">
              <YBButton btnText={"Save"}
                        btnClass={"btn btn-default save-btn"}
                        btnType="submit" />
              </div>
              </form>
            )}
          />
        </div>
      );
    }
    else {
      return (
        <div className="provider-config-container">
          Stay tuned for our Microsoft Azure integration.
        </div>
      )
    }
  }
}

export default withRouter(AzureProviderConfiguration);
