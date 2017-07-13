// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import {Field} from 'redux-form';
import {Row, Col, Collapse} from 'react-bootstrap';
import {YBInputField, YBButton, YBTextArea} from '../../../common/forms/fields';
var Dropzone = require('react-dropzone');

export default class OnPremProviderAndAccessKey extends Component {
  constructor(props) {
    super(props);
    this.state  = {privateKeyFile: {}, hostOptionsVisible: false}
    this.toggleAdditionalHostOptions = this.toggleAdditionalHostOptions.bind(this);
    this.privateKeyUpload = this.privateKeyUpload.bind(this);
    this.submitProviderKeyForm = this.submitProviderKeyForm.bind(this);
  }

  submitProviderKeyForm(vals) {
    this.props.setOnPremProviderAndAccessKey(vals);
  }

  toggleAdditionalHostOptions() {
    this.setState({hostOptionsVisible: !this.state.hostOptionsVisible});
  }

  privateKeyUpload(val) {
    this.setState({privateKeyFile: val[0]});
  }

  render() {
    const {handleSubmit, switchToJsonEntry} = this.props;
    var hostOptionsIndicator =
      <i className={this.state.hostOptionsVisible ? "fa fa-chevron-up": "fa fa-chevron-down"} />;

    let isReadOnly = this.props.isEditProvider;
    return (
      <div className="on-prem-provider-form-container">
        <form name="onPremConfigForm" onSubmit={handleSubmit(this.submitProviderKeyForm)}>
          <Row>
            <Col lg={5}>
              <div className="form-right-aligned-labels">
                <Field name="name" component={YBInputField} label="Provider Name" isReadOnly={isReadOnly}/>
                <Field name={"sshUser"} component={YBInputField} label=" SSH User" isReadOnly={isReadOnly}/>
                <Field name="privateKeyContent" component={YBTextArea} label="SSH Key" className="ssh-key-container" isReadOnly={isReadOnly}/>
              </div>
              <div className="add-host-options-container" onClick={this.toggleAdditionalHostOptions}>
                {hostOptionsIndicator} Additional Host Options
              </div>
            </Col>
            <Collapse in={this.state.hostOptionsVisible}>
              <Col lg={8}>
                <Col lg={12} className="ssh-key-entry-row">
                  <div className="host-item-label">
                    Password-Less SSH
                  </div>
                  <div>
                    Provide Your SSH Key to automatically register hosts
                    and manage YugaByte on those hosts.
                  </div>
                  <Dropzone onDrop={this.privateKeyUpload} className="btn btn-default">
                    <div>Choose File</div>
                  </Dropzone>
                  <span className="host-current-file-container">{this.state.privateKeyFile ? this.state.privateKeyFile.name : ""}</span>
                </Col>
                <Col lg={6}>
                  YugaByte Data Directory Path
                  <Field name={"directoryPath"} component={YBInputField}/>
                </Col>
              </Col>
            </Collapse>
          </Row>
          <div className="form-action-button-container">
            {switchToJsonEntry}
            <YBButton btnText={"Next"} btnType={"submit"} btnClass={"btn btn-default save-btn"}/>
          </div>
        </form>
      </div>
    )
  }
}
