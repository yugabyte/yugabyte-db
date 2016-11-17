// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import YBModal from '../../components/fields/YBModal';
import {Field, FieldArray } from 'redux-form';
import ItemList from './formcomponents/ItemList';
import {SOFTWARE_PACKAGE} from '../../config';
import YBInputField from '../fields/YBInputField';
import {isValidObject} from '../../utils/ObjectUtils';
import YBButton from './../fields/YBButton';
import {Row, Col} from 'react-bootstrap';

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
          <YBButton btnSize="sm" btnIcon="fa fa-times fa-fw" onClick={deleteRow}/>
        </Col>
      </Row>
    )
  }
}

class FlagItems extends Component {
  componentDidMount() {
    this.props.fields.push({});
  }
  render() {
    const { fields } = this.props;
    var addFlagItem = function() {
      fields.push({})
    }
    var gFlagsFieldList = fields.map(function(item, idx){
      return <FlagInput item={item} key={idx}
                        deleteRow={() => fields.remove(idx)} />
    })

    return (
      <div>
        {
          gFlagsFieldList
        }
        <YBButton btnClass="btn btn-sm universe-btn btn-default"
                  btnText="Add" btnIcon="fa fa-plus"
                  onClick={addFlagItem} />
      </div>
    )
  }
}


export default class RollingUpgradeForm extends Component {

  constructor(props) {
    super(props);
    this.setRollingUpgradeProperties = this.setRollingUpgradeProperties.bind(this);
  }

  setRollingUpgradeProperties(values) {
    const { universe: {visibleModal, currentUniverse: {universeDetails: {nodeDetailsSet}, universeUUID}}} = this.props;
    var nodeIndexArray = values.nodeItemList;
    var nodeNames = [];

    nodeDetailsSet.forEach(function(item, idx){
      if (isValidObject(nodeIndexArray[idx])) {
        nodeNames.push(item.nodeName);
      }
    });

    if (visibleModal === "softwareUpgradesModal") {
      values.taskType = "Software";
    } else if (visibleModal === "gFlagsModal") {
      values.taskType = "GFlags";
    } else {
       return;
    }
    values.nodeNames = nodeNames;
    values.universeUUID = universeUUID;
    delete values.nodeItemList;
    this.props.submitRollingUpgradeForm(values, universeUUID);
  }

  render() {
    var self = this;
    const {onHide, modalVisible, handleSubmit, universe: {visibleModal, currentUniverse: {universeDetails: {nodeDetailsSet}}}} = this.props;
    const submitAction = handleSubmit(self.setRollingUpgradeProperties);
    var title = "";
    var formBody = <span/>;
    var formCloseAction = function() {
      onHide();
      self.props.reset();
    }
    if (visibleModal === "softwareUpgradesModal") {
      title="Upgrade Software";
      formBody = <span>
                   <Col lg={12} className="form-section-title">
                     Software Package Version
                   </Col>
                   <Field name="ybServerPackage" component={YBInputField} defaultValue={SOFTWARE_PACKAGE}/>
                 </span>
    } else {
      title = "GFlags";
      formBody = <span>
                   <Col lg={12} className="form-section-title">
                     Set Flag
                   </Col>
                   <FieldArray name="gflags" component={FlagItems}/>
                 </span>
    }

    return (
      <YBModal visible={modalVisible} formName={"RollingUpgradeForm"}
               onHide={formCloseAction} title={title} onFormSubmit={submitAction}>
        {formBody}
        <Col lg={12} className="form-section-title">
          Nodes
        </Col>
        <FieldArray name="nodeItemList" component={ItemList} nodeList={nodeDetailsSet}/>
      </YBModal>
    )
  }
}
