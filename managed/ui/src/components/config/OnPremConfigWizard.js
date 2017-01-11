// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Row, Col } from 'react-bootstrap';
import { YBButton , YBInputField, YBSelect} from '../common/forms/fields';
import './stylesheets/OnPremiseProviderConfiguration.scss';
import {Field, FieldArray} from 'redux-form';
import {isValidObject} from '../../utils/ObjectUtils';

class YBHostDataCell extends Component {
  constructor(props) {
    super(props);
    this.deleteDataCell = this.deleteDataCell.bind(this);
  }
  deleteDataCell() {
    const {deleteItem, idx} = this.props;
    deleteItem(idx);
  }
  render() {
    const {item, activeClass} = this.props;
    var dataCellItem =
      <Row className="cell-container">
        <div className="host-data-cell">
          <Field name={`${item}.machine`} component={YBSelect}/>
          <Field name={`${item}.name`} component={YBInputField} placeHolder={"123.123.123.123"}/>
          <YBButton onClick={this.deleteDataCell} btnIcon={"fa fa-minus"} btnClass="btn btn-default delete-btn"/>
        </div>
      </Row>
    return (
      <div className={activeClass} onClick={this.setActiveDataCell}>
        {dataCellItem}
      </div>
    )
  }
}
class YBDataCell extends Component {

  constructor(props) {
    super(props);
    this.addDataCell = this.addDataCell.bind(this);
    this.editDataCell = this.editDataCell.bind(this);
    this.setActiveDataCell = this.setActiveDataCell.bind(this);
    this.deleteDataCell = this.deleteDataCell.bind(this);
  }

  addDataCell() {
    const {saveItem, idx} = this.props;
    saveItem(idx);
  }
  editDataCell() {
    const {editItem, idx} = this.props;
    editItem(idx);
  }
  deleteDataCell() {
    const {deleteItem, idx} = this.props;
    deleteItem(idx);
  }
  setActiveDataCell() {
    const {selectItem, idx} = this.props;
    selectItem(idx);
  }
  render() {
    const {item, fields, idx, editing, activeClass} = this.props;
    var dataCellItem = <span/>;

      if (editing) {
        dataCellItem =
          <Row className="cell-container">
            <Field name={`${item}.name`} component={YBInputField}/>
            <div className="cell-button-group">
              <YBButton onClick={this.addDataCell} btnIcon="fa fa-check fa-fw"
                        btnClass="btn btn-default config-action-btn"/>
              <YBButton btnIcon="fa fa-close fa-fw" onClick={this.deleteDataCell} btnClass="btn btn-default config-action-btn"/>
            </div>
          </Row>
      } else {
        dataCellItem =
          <Row className="cell-container">
            <div className="cell-item">{fields.get(idx).name}</div>
            <div className="cell-button-group">
              <YBButton onClick={this.editDataCell} btnIcon="fa fa-pencil fa-fw"
                        btnClass="btn btn-default config-action-btn"/>
              <YBButton onClick={this.deleteDataCell} btnIcon="fa fa-close fa-fw" btnClass="btn btn-default config-action-btn"/>
            </div>
          </Row>
      }

    return (
      <div className={activeClass} onClick={this.setActiveDataCell}>
        {dataCellItem}
      </div>
    )
  }
}

class YBHostDataList extends Component {
  constructor(props) {
    super(props);
    this.addHostData = this.addHostData.bind(this);
    this.deleteHostData = this.deleteHostData.bind(this);
    this.state = {currentSelectedHost: 0, currentEditingHost: 0};
  }
  componentWillMount() {
    const {fields} = this.props;
    if (fields.length === 0) {
      fields.push({});
    }
  }
  addHostData() {
    const {fields} = this.props;
    fields.push({});
    var fieldLen = fields.length;
    this.setState({currentSelectedHost: fieldLen});
    this.setState({currentEditingHost: fieldLen});
  }

  deleteHostData(idx) {
    const {fields} = this.props;
    fields.remove(idx);
  }

  render() {
    var self = this;
    const {fields, displayClass} = this.props;
    var hostDataList = <div>No Zone Selected</div>;
    if (isValidObject(fields)) {
      hostDataList = fields.map(function(hostItem, idx){
        var hostDisplayClass = "";
        var editing = false;
        return (
          <YBHostDataCell key={idx} item={hostItem} type={"Host"}
                          editing={editing} fields={fields} idx={idx}
                          deleteItem={self.deleteHostData}
                          activeClass={hostDisplayClass}/>
        )
      });
    }
    return (
      <div className={`host-list-container ${displayClass}`}>
        <Row className={"host-list-label"}>
          <Col lg={5}>
            Machine Type
          </Col>
          <Col lg={6} >
            IP Address
          </Col>
        </Row>
        <div className="list-column">
          {hostDataList}
        </div>
        <div onClick={this.addHostData} className={"add-list-item-label"}>
          Add Host<i className="fa fa-plus"></i>
        </div>
      </div>
    )
  }
}

class YBZoneDataList extends Component {
  constructor(props) {
    super(props);
    this.addZoneData = this.addZoneData.bind(this);
    this.selectZoneData = this.selectZoneData.bind(this);
    this.editZoneData = this.editZoneData.bind(this);
    this.setZoneActive = this.setZoneActive.bind(this);
    this.deleteZoneData = this.deleteZoneData.bind(this);
    this.state = {currentSelectedHost: 0,  currentEditingHost: 0};
  }

  componentWillMount() {
    const {fields} = this.props;
    if (fields.length > 0) {
      this.setState({currentSelectedHost: fields.length - 1, currentEditingHost: -1});
    } else {
      fields.push({});
    }
  }

  addZoneData() {
    const {fields} = this.props;
    fields.push({});
    var fieldLen = fields.length;
    this.setState({currentSelectedHost: fieldLen});
    this.setState({currentEditingHost: fieldLen});
  }
  selectZoneData(idx) {
    this.setState({currentEditingHost: -1});
  }
  editZoneData(idx) {
    this.setState({currentEditingHost: idx});
  }
  deleteZoneData(idx) {
    const {fields} = this.props;
    fields.remove(idx);
  }
  setZoneActive(idx) {
    this.setState({currentSelectedHost: idx});
  }

  render() {
    var self = this;
    const {fields, zoneDisplayClass} = this.props;
    var zoneDataList = <div>No Region Selected</div>
    if (isValidObject(fields)) {
      zoneDataList = fields.map(function(zoneItem, idx){
        var hostDisplayClass = "column-hidden";
        var zoneDisplayClass = "";
        if (idx === self.state.currentSelectedHost) {
          hostDisplayClass = "column-visible";
          zoneDisplayClass = "data-item-active";
        }
        var editing = false;
        if (idx === self.state.currentEditingHost) {
          editing = true;
        }
        return (
          <span key={idx}>
            <YBDataCell item={zoneItem} editing={editing} fields={fields} idx={idx} saveItem={self.selectZoneData}
                        editItem={self.editZoneData} selectItem={self.setZoneActive} deleteItem={self.deleteZoneData}
                        activeClass={zoneDisplayClass} />
            <FieldArray name={`${zoneItem}.hosts`} component={YBHostDataList} displayClass={hostDisplayClass}/>
          </span>
        )
      })
    }
    return (
      <div className={`zone-list-container ${zoneDisplayClass}`}>
        <div onClick={this.addZoneData} className={"add-list-item-label"}>
          Add Zone<i className="fa fa-plus"></i>
        </div>
        <div className="list-column">
          {zoneDataList}
        </div>
      </div>
      )
  }
}

class YBRegionDataList extends Component {
  constructor(props) {
    super(props);
    this.addRegionData = this.addRegionData.bind(this);
    this.selectRegionData = this.selectRegionData.bind(this);
    this.editRegionData = this.editRegionData.bind(this);
    this.deleteRegionData = this.deleteRegionData.bind(this);
    this.state = {currentSelectedZone: 0, currentEditingZone: 0};
    this.setRegionActive = this.setRegionActive.bind(this);
  }

  componentWillMount() {
    const {fields} = this.props;
    if (fields.length > 0) {
      this.setState({currentSelectedZone: fields.length - 1, currentEditingZone: -1});
    } else {
      fields.push({});
    }
  }

  addRegionData() {
    const {fields} = this.props;
    fields.push({});
    var fieldLen = fields.length;
    this.setState({currentSelectedZone: fieldLen});
    this.setState({currentEditingZone: fieldLen});
  }

  selectRegionData(idx) {
    this.setState({currentEditingZone: -1});
  }
  editRegionData(idx) {
    this.setState({currentEditingZone: idx});
  }
  setRegionActive(idx) {
    this.setState({currentSelectedZone: idx});
  }
  deleteRegionData(idx) {
    const {fields} = this.props;
    fields.remove(idx);
  }

  render() {
    var self = this;
    const {fields, currentRegionItem} = this.props;

    if (isValidObject(fields)) {
      var regionListData = fields.map(function (regionItem, idx) {
        var zoneDisplayClass="column-hidden";
        var regionDisplayClass = "";
        if (idx === self.state.currentSelectedZone) {
          zoneDisplayClass = "column-visible";
          regionDisplayClass="data-item-active";
        }
        var editing = false;
        if (idx === self.state.currentEditingZone) {
          editing = true;
        }
        return (
          <div key={idx}>
            <YBDataCell item={regionItem} editing={editing} fields={fields}
                        idx={idx} saveItem={self.selectRegionData}
                        editItem={self.editRegionData} selectItem={self.setRegionActive}
                        deleteItem={self.deleteRegionData} activeClass={regionDisplayClass}/>
            <FieldArray name={`${regionItem}.zones`} component={YBZoneDataList}
                        zoneDisplayClass={zoneDisplayClass} saveItem={self.selectRegionData}/>
          </div>
        )
      })
    }

    return (
      <div className={`region-list-container ${currentRegionItem}`}>
        <div onClick={this.addRegionData} className={"add-list-item-label"}>
          Add Region<i className="fa fa-plus"></i>
        </div>
        <div className="list-column">
          {regionListData}
        </div>
      </div>
        )
  }
}

export default class OnPremConfigWizard extends Component {
  componentWillMount() {
    this.props.initialize(this.props.config.onPremJsonFormData);
  }
  componentWillUnmount() {
    this.props.setOnPremJsonData({regions: this.props.formValues});
  }
  render() {
    const {onFormSubmit} = this.props;
    return (
      <form name="OnPremProviderConfigForm" onSubmit={onFormSubmit}>
        <Row className="form-data-container">
          <FieldArray name={"regions"} component={YBRegionDataList}
                      addRegionData={self.addRegionData} />
        </Row>
      </form>
    )
  }
}

