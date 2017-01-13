// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import {OnPremConfigWizardContainer, OnPremConfigJSONContainer} from '../../containers/config';
import { Row } from 'react-bootstrap';
import { YBButton } from '../common/forms/fields';

export default class OnPremConfiguration extends Component {

  constructor(props) {
    super(props);
    this.state = {'isJsonEntry': false};
    this.toggleJsonEntry = this.toggleJsonEntry.bind(this);
  }

  toggleJsonEntry() {
    this.setState({'isJsonEntry': !this.state.isJsonEntry})
  }

  render(){
    var ConfigurationDataForm = <OnPremConfigWizardContainer />;
    var btnText = this.state.isJsonEntry ? "Switch To Wizard View" : "Switch To JSON View";
    if (this.state.isJsonEntry) {
      ConfigurationDataForm = <OnPremConfigJSONContainer />
    }
    return (
      <div className="on-prem-provider-container">
        {ConfigurationDataForm}
        <Row className="form-action-button-container">
          <YBButton btnText={btnText} btnClass={"btn btn-default"} onClick={this.toggleJsonEntry}/>
          <YBButton btnText={"Submit"} btnType={"submit"}/>
        </Row>
      </div>
    )
  }
}
