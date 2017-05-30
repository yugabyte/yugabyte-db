// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Row, Col } from 'react-bootstrap';
import { Field, FieldArray } from 'redux-form';
import { YBInputField, YBButton, YBSelect } from '../../../common/forms/fields';


class OnPremListMachineTypes extends Component {
  constructor(props) {
    super(props);
    this.addMachineTypeRow = this.addMachineTypeRow.bind(this);
  }
  componentWillMount() {
    const {fields} = this.props;
    if (fields.length === 0) {
      this.props.fields.push({});
    }
  }
  addMachineTypeRow() {
    this.props.fields.push({});
  }
  removeMachineTypeRow(idx) {
    this.props.fields.remove(idx);
  }
  render() {
    const {fields} = this.props;
    var self = this;
    var volumeTypeOptions = [
      <option key={1} value="">Select</option>,
      <option key={2} value="EBS">EBS</option>,
      <option key={3} value="SSD">SSD</option>
    ]
    return (
      <div>
        { fields.map(function(fieldItem, fieldIdx){
          return (
            <Row key={`fieldMap${fieldIdx}`}>
              <Col lg={1}>
                <i className="fa fa-minus-circle on-prem-row-delete-btn" onClick={self.removeMachineTypeRow.bind(self, fieldIdx)}/>
              </Col>
              <Col lg={3}>
                <Field name={`${fieldItem}.code`} component={YBInputField}/>
              </Col>
              <Col lg={1}>
                <Field name={`${fieldItem}.numCores`}component={YBInputField}/>
              </Col>
              <Col lg={1}>
                <Field name={`${fieldItem}.memSizeGB`} component={YBInputField}/>
              </Col>
              <Col lg={1}>
                <Field name={`${fieldItem}.volumeSizeGB`} component={YBInputField}/>
              </Col>
              <Col lg={1}>
                <Field name={`${fieldItem}.volumeType`} component={YBSelect} options={volumeTypeOptions}/>
              </Col>
              <Col lg={4}>
                <Field name={`${fieldItem}.mountPath`} component={YBInputField}/>
              </Col>
            </Row>
          )
        })
        }
        <Row>
          <Col lg={1}>
            <i className="fa fa-plus-circle fa-2x on-prem-row-add-btn" onClick={this.addMachineTypeRow}/>
          </Col>
          <Col lg={3}>
            <a className="on-prem-add-link" onClick={this.addMachineTypeRow}>Add Machine Type</a>
          </Col>
        </Row>
      </div>
    )
  }
}

export default class OnPremMachineTypes extends Component {
  constructor(props) {
    super(props);
    this.submitOnPremForm = this.submitOnPremForm.bind(this);
  }
  submitOnPremForm(values) {
    this.props.submitOnPremMachineTypes(values);
  }

  render() {
    const {handleSubmit, switchToJsonEntry} = this.props;
    return (
      <div className="on-prem-provider-form-container">
        <form name="onPremMachineTypesConfigForm" onSubmit={handleSubmit(this.props.submitOnPremMachineTypes)}>
          <div className="on-prem-form-text">
            Add one or more machine types to define your hardware configuration.
          </div>
          <div className="form-field-grid">
            <Row>
              <Col lg={3} lgOffset={1}>
                Machine Type
              </Col>
              <Col lg={1}>
                Num Cores
              </Col>
              <Col lg={1}>
                Mem Size GB
              </Col>
              <Col lg={1}>
                Vol Size GB
              </Col>
              <Col lg={1}>
                Volume Type
              </Col>
              <Col lg={4}>
                Mount Paths <span className="row-head-subscript">Comma Separated</span>
              </Col>
            </Row>
            <div className="on-prem-form-grid-container">
              <FieldArray name="machineTypeList" component={OnPremListMachineTypes}/>
            </div>
          </div>
          <div className="form-action-button-container">
            {switchToJsonEntry}
            <YBButton btnText={"Next"} btnType={"submit"} btnClass={"btn btn-default save-btn"}/>
            <YBButton btnText={"Previous"}  btnClass={"btn btn-default back-btn"} onClick={this.props.prevPage}/>
          </div>
        </form>
      </div>
    );
  }
}

