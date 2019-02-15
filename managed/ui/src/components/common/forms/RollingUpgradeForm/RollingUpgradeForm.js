// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Field, FieldArray } from 'redux-form';
import { Row, Col, Tabs, Tab } from 'react-bootstrap';
import { YBModal, YBInputField, YBAddRowButton, YBSelectWithLabel, YBToggle, YBCheckBox } from '../fields';
import { isNonEmptyArray } from 'utils/ObjectUtils';
import {getPromiseState} from 'utils/PromiseUtils';
import './RollingUpgradeForm.scss';
import { getPrimaryCluster } from "../../../../utils/UniverseUtils";
import { isDefinedNotNull, isNonEmptyObject } from "../../../../utils/ObjectUtils";

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
      <div className="form-field-grid">
        {
          gFlagsFieldList
        }
        <YBAddRowButton 
          btnText="Add" 
          onClick={addFlagItem} />
      </div>
    );
  }
}


export default class RollingUpgradeForm extends Component {
  constructor(props) {
    super(props);
    this.toggleConfirmValidation = this.toggleConfirmValidation.bind(this);
    this.softwareVersionChanged = this.softwareVersionChanged.bind(this);
    this.state = {
      formConfirmed: false,
      pickedVersion: null
    };
  }

  softwareVersionChanged(value) {
    this.setState({
      pickedVersion: value
    });
  }

  toggleConfirmValidation = () => {
    this.setState({
      formConfirmed: !this.state.formConfirmed
    });
  }

  componentWillReceiveProps(nextProps) {
    const { universe: { rollingUpgrade, currentUniverse: { data: { universeUUID }}}} = nextProps;
    if (getPromiseState(rollingUpgrade).isSuccess() && getPromiseState(this.props.universe.rollingUpgrade).isLoading()) {
      this.props.fetchCurrentUniverse(universeUUID);
      this.props.fetchUniverseMetadata();
      this.props.fetchCustomerTasks();
      this.props.fetchUniverseTasks(universeUUID);
    }
  }

  componentDidMount = () => {
    const { softwareVersions } = this.props;
    this.softwareVersionChanged(softwareVersions[0]);
  }

  setRollingUpgradeProperties = values => {
    const {
      reset,
      modal: {
        visibleModal
      },
      universe: {
        currentUniverse: {
          data: {
            universeDetails: {clusters, nodePrefix},
            universeUUID
          }
        }
      }
    } = this.props;
    const payload = {};
    if (visibleModal === "softwareUpgradesModal") {
      payload.taskType = "Software";
    } else if (visibleModal === "gFlagsModal") {
      payload.taskType = "GFlags";
    } else {
      return;
    }
    const primaryCluster = getPrimaryCluster(clusters);
    if (!isDefinedNotNull(primaryCluster)) {
      return;
    }
    payload.ybSoftwareVersion = values.ybSoftwareVersion;
    payload.rollingUpgrade = values.rollingUpgrade;
    payload.universeUUID = universeUUID;
    payload.nodePrefix = nodePrefix;
    let masterGFlagList = [];
    let tserverGFlagList = [];
    if (isNonEmptyArray(values.masterGFlags)) {
      masterGFlagList = values.masterGFlags.map(function(masterFlag, masterIdx){
        if (masterFlag.name && masterFlag.value) {
          return {name: masterFlag.name, value: masterFlag.value};
        } else {
          return null;
        }
      }).filter(Boolean);
    }
    if (isNonEmptyArray(values.tserverGFlags)) {
      tserverGFlagList = values.tserverGFlags.map(function(tserverFlag, tserverIdx){
        if (tserverFlag.name && tserverFlag.value) {
          return {name: tserverFlag.name, value: tserverFlag.value};
        } else {
          return null;
        }
      }).filter(Boolean);
    }
    primaryCluster.userIntent.ybSoftwareVersion = values.ybSoftwareVersion;
    primaryCluster.userIntent.masterGFlags = masterGFlagList;
    primaryCluster.userIntent.tserverGFlags = tserverGFlagList;
    payload.clusters = [primaryCluster];
    payload.sleepAfterMasterRestartMillis = values.timeDelay;
    payload.sleepAfterTServerRestartMillis = values.timeDelay;
    this.props.submitRollingUpgradeForm(payload, universeUUID, reset);
  };

  render() {
    const self = this;
    const {onHide, modalVisible, handleSubmit, universe, modal: { visibleModal }, 
      universe: { error}, resetRollingUpgrade, softwareVersions} = this.props;

    let currentVersion = null;
    if(isDefinedNotNull(universe.currentUniverse.data) && isNonEmptyObject(universe.currentUniverse.data)) {
      const primaryCluster = getPrimaryCluster(universe.currentUniverse.data.universeDetails.clusters);
      currentVersion = primaryCluster && (primaryCluster.userIntent.ybSoftwareVersion || undefined);
    }

    const submitAction = handleSubmit(self.setRollingUpgradeProperties);
    let title = "";
    let formBody = <span/>;
    const softwareVersionOptions = softwareVersions.map(function(item, idx){
      return <option key={idx} disabled={ item===currentVersion ? true : false } value={item}>{item}</option>;
    });

    const formCloseAction = function() {
      onHide();
      self.props.reset();
      self.setState({
        formConfirmed: false,
        formValidated: false,
      });
    };
    if (visibleModal === "softwareUpgradesModal") {
      title="Upgrade Software";
      formBody = (
        <span>
          <Col lg={12} className="form-section-title">
            Software Package Version
          </Col>
          <div className="form-right-aligned-labels">
            <Field name="rollingUpgrade" component={YBToggle} label="Rolling Upgrade" />
            <Field name="timeDelay" component={YBInputField}
                               label="Upgrade Delay Between Servers (secs)" />
            <Field name="ybSoftwareVersion" type="select" component={YBSelectWithLabel}
                   options={softwareVersionOptions} label="Server Version"
                   onInputChanged={this.softwareVersionChanged}/>
          </div>
        </span>
      );
    } else {
      title = "GFlags";
      formBody = (
        <div>
          <Tabs defaultActiveKey={1} className="gflag-display-container" id="gflag-container" >
            <Tab eventKey={1} title="Master" className="gflag-class-1" bsClass="gflag-class-2">
              <FieldArray name="masterGFlags" component={FlagItems} resetRollingUpgrade={resetRollingUpgrade}/>
            </Tab>
            <Tab eventKey={2} title="T-Server">
              <FieldArray name="tserverGFlags" component={FlagItems} resetRollingUpgrade={resetRollingUpgrade}/>
            </Tab>
          </Tabs>
          <div className="form-right-aligned-labels top-10 time-delay-container">
            <Field name="rollingUpgrade" component={YBToggle} label="Rolling Upgrade"/>
            <Field name="timeDelay" component={YBInputField}
                   label="Rolling Upgrade Delay Between Servers (secs)" />
          </div>
        </div>
      );
    }
    return (
      visibleModal === "softwareUpgradesModal" ?
        <YBModal visible={modalVisible} formName={"RollingUpgradeForm"}
                onHide={formCloseAction} submitLabel={'Upgrade'} showCancelButton={true} title={title} onFormSubmit={submitAction} error={error}
                footerAccessory={ this.state.pickedVersion !== currentVersion
                                    ? <YBCheckBox label={"Confirm software upgrade"} className="footer-accessory" input={{onChange: this.toggleConfirmValidation}} />
                                    : <span>{"Latest software is installed"}</span>} asyncValidating={!this.state.formConfirmed}>
          {formBody}
        </YBModal> :
        <YBModal visible={modalVisible} formName={"RollingUpgradeForm"}
                  onHide={formCloseAction} title={title} onFormSubmit={submitAction} error={error}>
          {formBody}
        </YBModal>
    );
  }
}
