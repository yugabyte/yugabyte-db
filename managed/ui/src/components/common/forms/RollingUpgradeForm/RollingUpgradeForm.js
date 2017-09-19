// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import {Field, FieldArray } from 'redux-form';
import {Row, Col, Tabs, Tab} from 'react-bootstrap';
import { YBButton, YBModal, YBInputField, YBCheckBox, YBSelectWithLabel } from '../fields';
import { isValidObject, isNonEmptyArray, isDefinedNotNull } from 'utils/ObjectUtils';
import './RollingUpgradeForm.scss';

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
    const addFlagItem = function() {
      fields.push({});
    };
    const gFlagsFieldList = fields.map((item, idx) => (
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

  setRollingUpgradeProperties(values) {
    const { universe: {visibleModal, currentUniverse: {data: {universeDetails: {nodeDetailsSet,
      userIntent}, universeUUID}}}, reset} = this.props;
    const nodeNames = [];
    const payload = {};
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
    let gflagList = [];
    if (isNonEmptyArray(values.masterGFlags)) {
      gflagList = values.masterGFlags.map((flag)=>{
        if (flag.name && flag.value) {
          return Object.assign({type: "master"}, flag);
        } else {
          return null;
        }
      }).filter(Boolean);
    }
    if (isNonEmptyArray(values.tserverGFlags)) {
      gflagList = gflagList.concat(
        values.tserverGFlags.map((flag)=>{
          if (flag.name && flag.value) {
            return Object.assign({type: "tserver"}, flag);
          } else {
            return null;
          }
        }).filter(Boolean)
      );
    }
    payload.gflags = gflagList;
    payload.sleepAfterMasterRestartMillis = values.timeDelay * 1000;
    payload.sleepAfterTServerRestartMillis = values.timeDelay * 1000;
    this.props.submitRollingUpgradeForm(payload, universeUUID, reset);
  }

  render() {
    const self = this;
    const {onHide, modalVisible, handleSubmit, universe: {visibleModal,
      error, currentUniverse: {data: {universeDetails}}}, resetRollingUpgrade, softwareVersions} = this.props;
    const submitAction = handleSubmit(self.setRollingUpgradeProperties);
    let title = "";
    let formBody = <span/>;
    const softwareVersionOptions = softwareVersions.map(function(item, idx){
      return <option key={idx} value={item}>{item}</option>;
    });
    const formCloseAction = function() {
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
        <div>
          <Tabs defaultActiveKey={2} className="gflag-display-container" id="gflag-container" >
            <Tab eventKey={1} title="Master" className="gflag-class-1" bsClass="gflag-class-2">
              <FieldArray name="masterGFlags" component={FlagItems} resetRollingUpgrade={resetRollingUpgrade}/>
            </Tab>
            <Tab eventKey={2} title="T-Server">
              <FieldArray name="tserverGFlags" component={FlagItems} resetRollingUpgrade={resetRollingUpgrade}/>
            </Tab>
          </Tabs>
          <div className="form-right-aligned-labels top-10 time-delay-container">
            <Field name="timeDelay" component={YBInputField} label="Rolling Update Delay Between Servers (secs)"/>
          </div>
        </div>
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
    let nodeCheckList = <Field name={"check"} component={YBCheckBox}/>;
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
