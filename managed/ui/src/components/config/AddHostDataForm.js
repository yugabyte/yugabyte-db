// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import {YBModal, YBInputField} from '../common/forms/fields';
import {Field} from 'redux-form';
import { Row, Col } from 'react-bootstrap';
var Dropzone = require('react-dropzone');

export default class AddHostDataForm extends Component {
  constructor(props) {
    super(props);
    this.submitAddHostProperties = this.submitAddHostProperties.bind(this);
    this.onDrop = this.onDrop.bind(this);
    this.state = {privateKeyVal: '', privateKeyFileName: 'No File Selected'};
  }
  submitAddHostProperties(vals) {
    // Submit Form Vals
  }
  onDrop(files) {
    var self = this;
    var fileVal = files[0];

    this.setState({privateKeyFileName: fileVal.name});
    var reader = new FileReader();

    reader.onload = (function(theFile) {
      return function(e) {
        self.setState({privateKeyVal: e.target.result});
      };
    })(fileVal);

    reader.readAsText(fileVal);
  }
  render() {
    const {handleSubmit, visible, onHide} = this.props;
    return (
      <div className="add-host-data-container">
        <YBModal formName={"AdditionalHostDataForm"} visible={visible}
                 title={"Additional Host Options"} onFormSubmit={handleSubmit(this.submitAddHostProperties)}
                 onHide={onHide}>
        <div className="host-item-label">
          Password-Less SSH
        </div>
        <div>
          Provide Your SSH Key to automatically register hosts
          and manage YugaByte on those hosts.
        </div>
        <Row className={"data-row-input-container"}>
          <Col lg={3}>
            <Dropzone onDrop={this.onDrop} className="btn btn-default">
              <div>Choose File</div>
            </Dropzone>
          </Col>
          <Col lg={9}>
            <div className="host-current-file-container">{this.state.privateKeyFileName}</div>
          </Col>
        </Row>
        <Row className={"data-row-input-container"}>
          <Col lg={7}>
            SSH User (root or passwordless SUDO account)
          </Col>
          <Col lg={5}>
            <Field name={"rootUserName"} component={YBInputField}/>
          </Col>
        </Row>
        <div className="host-item-label">YugaByte Data Directory Path</div>
        <Field name={"directoryPath"} component={YBInputField}/>
      </YBModal>
      </div>
    )
  }
}
