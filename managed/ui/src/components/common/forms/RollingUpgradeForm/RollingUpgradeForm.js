// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import {Field, FieldArray } from 'redux-form';
import {Row, Col} from 'react-bootstrap';
import { YBButton, YBModal, YBInputField, YBCheckBox, YBSelectWithLabel } from '../fields';
import { isValidObject, isNonEmptyArray, isDefinedNotNull } from 'utils/ObjectUtils';

class FlagInput extends Component {
  render() {
    const {deleteRow, item} = this.props;
    return (
      <Row>
        <Col lg={5}>
          <Field name={`${item}.name`} component={YBInputField} className="input-sm" placeHolder="GFlag Name"/>
        </Col>
        <Col lg={5}>
          <Field name={`${item}.value`} component={YBInputField} className="input-sm" placeHolder="Value"/>
        </Col>
        <Col lg={1}>
          <i className="fa fa-times fa-fw delete-row-btn" onClick={deleteRow}/>
        </Col>
      </Row>
    );
  }
}

class FlagItems extends Component {
  componentWillMount() {
    if (this.props.fields.length === 0) {
      this.props.fields.push({});
    }
  }

  render() {
    const { fields } = this.props;
    var addFlagItem = function() {
      fields.push({});
    };
    var gFlagsFieldList = fields.map((item, idx) => (
      <FlagInput item={item} key={idx} deleteRow={() => fields.remove(idx)} />
    ));

    return (
      <div>
        {
          gFlagsFieldList
        }
        <YBButton btnClass="btn btn-sm universe-btn btn-default"
                  btnText="Add" btnIcon="fa fa-plus"
                  onClick={addFlagItem} />
      </div>
    );
  }
}


export default class RollingUpgradeForm extends Component {
  constructor(props) {
    super(props);
    this.setRollingUpgradeProperties = this.setRollingUpgradeProperties.bind(this);
  }
  componentWillUnmount() {
    this.props.reset();
  }

  setRollingUpgradeProperties(values) {
    const { universe: {visibleModal, currentUniverse: {data: {universeDetails: {nodeDetailsSet,
      userIntent}, universeUUID}}}} = this.props;
    let nodeNames = [];
    let payload = {};
    nodeDetailsSet.forEach((item) => {
      if (!isValidObject(values[item.nodeName]) || values[item.nodeName] !== false) {
        nodeNames.push(item.nodeName);
      }
    });
    if (visibleModal === "softwareUpgradesModal") {
      payload.taskType = "Software";
    } else if (visibleModal === "gFlagsModal") {
      payload.taskType = "GFlags";
    } else {
      return;
    }
    payload.ybSoftwareVersion = values.ybSoftwareVersion;
    payload.nodeNames = nodeNames;
    payload.universeUUID = universeUUID;
    payload.userIntent = userIntent;
    if (isNonEmptyArray(values.gflags)) {
      payload.gflags = values.gflags;
    }
    this.props.submitRollingUpgradeForm(payload, universeUUID);
    this.props.reset();
  }

  render() {
    var self = this;
    const {onHide, modalVisible, handleSubmit, universe: {visibleModal,
      error, currentUniverse: {data: {universeDetails}}}, resetRollingUpgrade, softwareVersions} = this.props;
    const submitAction = handleSubmit(self.setRollingUpgradeProperties);
    var title = "";
    var formBody = <span/>;
    var softwareVersionOptions = softwareVersions.map(function(item, idx){
      return <option key={idx} value={item}>{item}</option>;
    });
    var formCloseAction = function() {
      onHide();
      self.props.reset();
    };
    if (visibleModal === "softwareUpgradesModal") {
      title="Upgrade Software";
      formBody = (
        <span>
          <Col lg={12} className="form-section-title">
            Software Package Version
          </Col>
          <Field name="ybSoftwareVersion" type="select" component={YBSelectWithLabel}
            options={softwareVersionOptions} label="Server Version"
            onInputChanged={this.softwareVersionChanged}/>
        </span>
      );
    } else {
      title = "GFlags";
      formBody = (
        <span>
          <Col lg={12} className="form-section-title">
            Set Flag
          </Col>
          <FieldArray name="gflags" component={FlagItems} resetRollingUpgrade={resetRollingUpgrade}/>
        </span>
      );
    }

    let itemList = <span/>;
    if (isDefinedNotNull(universeDetails) && isNonEmptyArray(universeDetails.nodeDetailsSet)) {
      itemList = <ItemList nodeList={universeDetails.nodeDetailsSet}/>;
    }
    return (
      <YBModal visible={modalVisible} formName={"RollingUpgradeForm"}
               onHide={formCloseAction} title={title} onFormSubmit={submitAction} error={error}>
        {formBody}
        <Col lg={12} className="form-section-title">
          Nodes
        </Col>
        {itemList}
      </YBModal>
    );
  }
}

class ItemList extends Component {
  render() {
    const {nodeList} = this.props;
    var nodeCheckList = <Field name={"check"} component={YBCheckBox}/>;
    if (isNonEmptyArray(nodeList)) {
      nodeCheckList =
        nodeList.map(function (item, idx) {
          return (
            <Col lg={4} key={idx}>
              <Field name={item.nodeName} component={YBCheckBox} label={item.nodeName} checkState={true}/>
            </Col>
          );
        });
    }
    return (
      <div>
        {nodeCheckList}
      </div>
    );
  }
}
