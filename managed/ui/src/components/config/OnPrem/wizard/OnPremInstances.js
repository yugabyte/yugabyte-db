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
          <Col lg={3}>
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
                <Col lg={3}>
                  <Field name={`${instanceTypeItem}.zone`} component={YBSelect} options={zoneOptions}/>
                </Col>
                <Col lg={3}>
                  <Field name={`${instanceTypeItem}.machineType`} component={YBSelect} options={machineTypeOptions}/>
                </Col>
                <Col lg={5}>
                  <Field name={`${instanceTypeItem}.instanceTypeIPs`} component={YBInputField}/>
                </Col>
                <Col lg={1}>
                  <YBButton btnIcon="fa fa-minus" onClick={self.removeRow.bind(self, instanceTypeIdx)}/>
                </Col>
              </Row>
            )
          })
        }
        <Row>
          <Col lg={12} onClick={this.addRow}>
            <i className="fa fa-plus-circle add-instance-btn"/>
            Add Zone
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
      zoneOptions.push(<option key={-1} value={""}>Select</option>);
      machineTypeOptions.push(<option key={-1} value={""}>Select</option>);
      return (
        <Row key={regionItem.code}>
          <Col lg={12}><div className="instance-region-type">{regionItem.code}</div></Col>
          <FieldArray name={`instances.${regionItem.code}`} component={InstanceTypeForRegion}
                      zoneOptions={zoneOptions} machineTypeOptions={machineTypeOptions}/>
        </Row>
      )
    }) : null;
    return (
      <div>
        <form name="onPremInstancesConfigForm" onSubmit={handleSubmit(this.props.setOnPremInstances)}>
          <Row className="on-prem-provider-form-container">
            <Row>
              <Col lgOffset={1} className="on-prem-form-text">Enter IP Addresses for the instances of each zone and machine type.</Col>
            </Row>
            {regionFormTemplate}
            </Row>
            <Row>
              <Col lg={12}>
                {switchToJsonEntry}
                <ButtonGroup className="pull-right">
                  <YBButton btnText={"Previous"}  btnClass={"btn btn-default save-btn "} onClick={this.props.prevPage}/>
                  <YBButton btnText={"Next"} btnType={"submit"} btnClass={"btn btn-default save-btn"}/>
                </ButtonGroup>
              </Col>
            </Row>
        </form>
      </div>
    )
  }
}
