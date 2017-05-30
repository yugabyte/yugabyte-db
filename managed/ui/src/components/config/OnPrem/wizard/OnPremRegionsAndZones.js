// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Row, Col } from 'react-bootstrap';
import { Field, FieldArray } from 'redux-form';
import { YBInputField, YBButton, YBSelect } from '../../../common/forms/fields';
import _ from 'lodash';

class OnPremListRegionsAndZones extends Component {
  constructor(props) {
    super(props);
    this.addRegionZoneTypeRow = this.addRegionZoneTypeRow.bind(this);
  }
  componentWillMount() {
    const {fields} = this.props;
    if (fields.length === 0) {
      this.props.fields.push({});
    }
  }
  addRegionZoneTypeRow() {
    this.props.fields.push({});
  }
  removeRegionZoneTypeRow(idx) {
    this.props.fields.remove(idx);
  }
  render() {
    const {fields} = this.props;
    var self = this;
    // TODO Replace this with API lookup to local DB City to LatLong Conversion
    var onPremRegionLocations = [
      <option value="" key={0}>Select</option>,
      <option value="37, -121" key={1}>US West</option>,
      <option value="40, -74" key={2}>US East</option>,
      <option value="48, -118" key={3}>US North</option>,
      <option value="28, -99" key={4}>US South</option>,
    ]
    return (
      <div>
        { fields.map(function(fieldItem, fieldIdx){
          return (
            <Row key={`region-zone-${fieldIdx}`}>
              <Col lg={1}>
                <i className="fa fa-minus-circle on-prem-row-delete-btn" onClick={self.removeRegionZoneTypeRow.bind(self, fieldIdx)}/>
              </Col>
              <Col lg={3}>
                <Field name={`${fieldItem}.code`} component={YBInputField}/>
              </Col>
              <Col lg={3}>
                <Field name={`${fieldItem}.location`} component={YBSelect} options={onPremRegionLocations}/>
              </Col>
              <Col lg={5}>
                <Field name={`${fieldItem}.zones`} component={YBInputField}/>
              </Col>
            </Row>
          )
        })
        }
        <Row>
          <Col lg={1}>
            <i className="fa fa-plus-circle fa-2x on-prem-row-add-btn" onClick={this.addRegionZoneTypeRow}/>
          </Col>
          <Col lg={3}>
            <a className="on-prem-add-link" onClick={this.addRegionZoneTypeRow}>Add Region</a>
          </Col>
        </Row>
      </div>
    )
  }
}

export default class OnPremRegionsAndZones extends Component {
  constructor(props) {
    super(props);
    this.createOnPremRegionsAndZones = this.createOnPremRegionsAndZones.bind(this);
  }
  createOnPremRegionsAndZones(vals) {
    this.props.setOnPremRegionsAndZones(vals);
  }

  render() {
    const {handleSubmit, switchToJsonEntry} = this.props;
    return (
      <div className="on-prem-provider-form-container">
        <form name="onPremRegionsAndZonesForm" onSubmit={handleSubmit(this.createOnPremRegionsAndZones)}>
          <div className="on-prem-form-text">
            Add One or More Regions, each with one or more zones.
          </div>
          <div className="form-field-grid">
            <Row>
              <Col lg={3} lgOffset={1}>
                Region Name
              </Col>
              <Col lg={3}>
                GeoLocation <span className="row-head-subscript">Lat/Long or City/State/County</span>
              </Col>
              <Col lg={5}>
                Zone Names <span className="row-head-subscript">Comma Separated</span>
              </Col>
            </Row>
            <div className="on-prem-form-grid-container">
              <FieldArray name="regionsZonesList" component={OnPremListRegionsAndZones}/>
            </div>
          </div>
          <div className="form-action-button-container">
            {switchToJsonEntry}
            <YBButton btnText={"Next"} btnType={"submit"} btnClass={"btn btn-default save-btn"}/>
            <YBButton btnText={"Previous"}  btnClass={"btn btn-default back-btn"} onClick={this.props.prevPage}/>
          </div>
        </form>
      </div>
    )
  }
}

