// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { OnPremConfigWizardContainer, OnPremConfigJSONContainer, AddHostDataFormContainer } from '../../config';
import { YBButton } from '../../common/forms/fields';

export default class OnPremConfiguration extends Component {

  constructor(props) {
    super(props);
    this.state = {'isJsonEntry': false, isAdditionalHostOptionsOpen: false};
    this.toggleJsonEntry = this.toggleJsonEntry.bind(this);
    this.toggleAdditionalOptionsModal = this.toggleAdditionalOptionsModal.bind(this);
  }

  toggleJsonEntry() {
    this.setState({'isJsonEntry': !this.state.isJsonEntry})
  }

  toggleAdditionalOptionsModal() {
    this.setState({isAdditionalHostOptionsOpen: !this.state.isAdditionalHostOptionsOpen});
  }

  render(){
    var ConfigurationDataForm = <OnPremConfigWizardContainer />;
    var btnText = this.state.isJsonEntry ? "Switch To Wizard View" : "Switch To JSON View";
    if (this.state.isJsonEntry) {
      ConfigurationDataForm = <OnPremConfigJSONContainer />
    }
    var hostDataForm = <AddHostDataFormContainer visible={this.state.isAdditionalHostOptionsOpen}
                                                 onHide={this.toggleAdditionalOptionsModal}/>;
    return (
      <div className="on-prem-provider-container">
        {hostDataForm}
        {ConfigurationDataForm}
        <div className="form-action-button-container">
          <YBButton btnText={btnText} btnClass={"btn btn-default"} onClick={this.toggleJsonEntry}/>
          <YBButton btnText={"Additional Host Options"} onClick={this.toggleAdditionalOptionsModal}/>
          <YBButton btnClass="pull-right btn btn-default bg-orange" btnText={"Save"} btnType={"submit"}/>
        </div>
      </div>
    )
  }
}
