// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Alert } from 'react-bootstrap';
import { OnPremConfigWizardContainer, OnPremConfigJSONContainer, AddHostDataFormContainer } from '../../config';
import { YBButton } from '../../common/forms/fields';
import emptyDataCenterConfig from '../templates/EmptyDataCenterConfig.json';

export default class OnPremConfiguration extends Component {

  constructor(props) {
    super(props);
    const emptyJsonPretty = JSON.stringify(JSON.parse(JSON.stringify(emptyDataCenterConfig)), null, 2);
    this.state = {
      'isJsonEntry': false,
      isAdditionalHostOptionsOpen: false,
      configJsonVal: emptyJsonPretty,
      createSucceeded: false,
      createFailed: false
    };
    this.toggleJsonEntry = this.toggleJsonEntry.bind(this);
    this.toggleAdditionalOptionsModal = this.toggleAdditionalOptionsModal.bind(this);
    this.submitJson = this.submitJson.bind(this);
    this.updateConfigJsonVal = this.updateConfigJsonVal.bind(this);
  }

  toggleJsonEntry() {
    this.setState({'isJsonEntry': !this.state.isJsonEntry})
  }

  toggleAdditionalOptionsModal() {
    this.setState({isAdditionalHostOptionsOpen: !this.state.isAdditionalHostOptionsOpen});
  }

  updateConfigJsonVal(newConfigJsonVal) {
    this.setState({configJsonVal: newConfigJsonVal});
  }

  submitJson() {
    if (this.state.isJsonEntry) {
      this.props.createOnPremProvider(this.state.configJsonVal);
    }
  }

  render(){
    let ConfigurationDataForm = <OnPremConfigWizardContainer />;
    let btnText = this.state.isJsonEntry ? "Switch To Wizard View" : "Switch To JSON View";
    if (this.state.isJsonEntry) {
      ConfigurationDataForm = <OnPremConfigJSONContainer updateConfigJsonVal={this.updateConfigJsonVal}
                                                         configJsonVal={this.state.configJsonVal} />
    }
    let hostDataForm = <AddHostDataFormContainer visible={this.state.isAdditionalHostOptionsOpen}
                                                 onHide={this.toggleAdditionalOptionsModal}/>;

    let message = "";
    if (this.props.cloud.createOnPremSucceeded) {
      message = <Alert bsStyle="success">Create On Premise Provider Succeeded</Alert>
    } else if (this.props.cloud.createOnPremFailed) {
      message = <Alert bsStyle="danger">Create On Premise Provider Failed</Alert>
    }

    return (
      <div className="on-prem-provider-container">
        {message}
        {hostDataForm}
        {ConfigurationDataForm}
        <div className="form-action-button-container">
          <YBButton btnText={btnText} btnClass={"btn btn-default"} onClick={this.toggleJsonEntry}/>
          <YBButton btnText={"Additional Host Options"} onClick={this.toggleAdditionalOptionsModal}/>
          <YBButton btnClass="pull-right btn btn-default bg-orange" btnText={"Save"} onClick={this.submitJson}/>
        </div>
      </div>
    )
  }
}
