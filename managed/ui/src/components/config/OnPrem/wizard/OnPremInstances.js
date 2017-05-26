// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import {Row, Col, ButtonGroup} from 'react-bootstrap';
import {YBSelect, YBButton, YBInputField} from '../../../common/forms/fields';
import {Field, FieldArray} from 'redux-form';
import {isValidArray} from '../../../../utils/ObjectUtils';
import _ from 'lodash';

class InstanceTypeForRegion extends Component {
  constructor(props) {
    super(props);
    this.addRow = this.addRow.bind(this);
  }
  addRow() {
    this.props.fields.push({});
  }
  componentWillMount() {
    const {fields} = this.props;
    if (fields.length === 0) {
      this.props.fields.push({});
    }
  }
  removeRow(instanceTypeIdx) {
    this.props.fields.remove(instanceTypeIdx);
  }
  render() {
    var self = this;
    const {fields, zoneOptions, machineTypeOptions} = this.props;
    return (
      <div className="instance-row-container">
        <Row>
          <Col lg={3} lgOffset={1}>
            Zone
          </Col>
          <Col lg={3}>
            Machine Type
          </Col>
          <Col lg={5}>
            Instances <span className="row-head-subscript">Comma Separated IP Addresses</span>
          </Col>
        </Row>
        {
          fields.map(function(instanceTypeItem, instanceTypeIdx){
            return (
              <Row key={instanceTypeIdx}>
                <Col lg={1}>
                  <i className="fa fa-minus-circle on-prem-row-delete-btn" onClick={self.removeRow.bind(self, instanceTypeIdx)}/>
                </Col>
                <Col lg={3}>
                  <Field name={`${instanceTypeItem}.zone`} component={YBSelect} options={zoneOptions}/>
                </Col>
                <Col lg={3}>
                  <Field name={`${instanceTypeItem}.machineType`} component={YBSelect} options={machineTypeOptions}/>
                </Col>
                <Col lg={5}>
                  <Field name={`${instanceTypeItem}.instanceTypeIPs`} component={YBInputField}/>
                </Col>
              </Row>
            )
          })
        }
        <Row>
          <Col lg={1}>
            <i className="fa fa-plus-circle fa-2x on-prem-row-add-btn" onClick={this.addRow} />
          </Col>
          <Col lg={3}>
            <a className="on-prem-add-link" onClick={this.addRow}>Add Zone or Machine Type</a>
          </Col>
        </Row>
      </div>
    )
  }
}

export default class OnPremInstances extends Component {
  constructor(props) {
    super(props);
    this.submitInstances = this.submitInstances.bind(this);
  }
  submitInstances(formVals) {
    var submitPayload = [];
    Object.keys(formVals.instances).forEach(function(key){
      formVals.instances[key].forEach(function(instanceTypeItem){
        instanceTypeItem.instanceTypeIPs.split(",").forEach(function(ipItem, ipIdx){
          submitPayload.push({ip: ipItem, zone: instanceTypeItem.zone, region: key, instanceType: instanceTypeItem.machineType});
        });
      });
    });
  }

  render() {
    const {handleSubmit, switchToJsonEntry} = this.props;
    const {onPremJsonFormData} = this.props;
    if (!onPremJsonFormData) {
      return <span/>;
    }
    var regionFormTemplate = onPremJsonFormData.regions ? onPremJsonFormData.regions.map(function(regionItem, idx){
      var zoneOptions = regionItem.zones.map(function(zoneItem, zoneIdx){
        return <option key={zoneItem+zoneIdx} value={zoneItem}>{zoneItem}</option>});
      var machineTypeOptions = onPremJsonFormData.instanceTypes.map(function(machineTypeItem, mcIdx){
        return <option key={machineTypeItem+mcIdx} value={machineTypeItem.instanceTypeCode}>{machineTypeItem.instanceTypeCode}</option>;
      });
      zoneOptions.unshift(<option key={-1} value={""}>Select</option>);
      machineTypeOptions.unshift(<option key={-1} value={""}>Select</option>);
      return (
        <div>
          <div className="instance-region-type">{regionItem.code}</div>
          <div className="form-field-grid">
            <FieldArray name={`instances.${regionItem.code}`} component={InstanceTypeForRegion}
                        zoneOptions={zoneOptions} machineTypeOptions={machineTypeOptions}/>
          </div>
        </div>
      )
    }) : null;
    return (
      <div className="on-prem-provider-form-container">
        <form name="onPremInstancesConfigForm" onSubmit={handleSubmit(this.props.setOnPremInstances)}>
          <div className="on-prem-form-text">
            Enter IP Addresses for the instances of each zone and machine type.
          </div>
          {regionFormTemplate}
          <div className="form-action-button-container">
            {switchToJsonEntry}
            <YBButton btnText={"Finish"} btnType={"submit"} btnClass={"btn btn-default save-btn"}/>
            <YBButton btnText={"Previous"}  btnClass={"btn btn-default back-btn"} onClick={this.props.prevPage}/>
          </div>
        </form>
      </div>
    )
  }
}
