// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { Modal } from 'react-bootstrap';
import Select from 'react-select';
import 'react-select/dist/react-select.css';
import $ from 'jquery';
import { isValidObject } from '../utils/ObjectUtils';
import NumericInput from 'react-numeric-input';

class UniverseModal extends Component {

  static propTypes = {
    type: PropTypes.oneOf(['Edit', 'Create']).isRequired,
  }

  constructor(props) {
    super(props);
    this.showModal = this.showModal.bind(this);
    this.closeModal = this.closeModal.bind(this);
    this.providerChanged = this.providerChanged.bind(this);
    this.multiAZChanged = this.multiAZChanged.bind(this);
    this.regionChanged = this.regionChanged.bind(this);
    this.universeAction = this.universeAction.bind(this);
    this.state = {
      showModal: false,
      regionSelected: []
    };
  }

  componentDidMount() {
    var self = this;
    this.props.getProviderListItems();
    if (this.props.type === "Edit") {
      var currentProvider = "";
      var currentMultiAz = false;
      var universeName = "";
      // If Edit Universe is called in the context of Universe List
      if(self.props.uuid) {
        self.props.universe.universeList.map(function (universeItem, idx) {
          if (universeItem.universeUUID === self.props.uuid) {
            self.props.getRegionListItems(universeItem.provider.uuid, universeItem.universeDetails.userIntent.isMultiAZ);
            self.props.getInstanceTypeListItems(universeItem.provider.uuid);
            return (
              self.setState({
                universeName: universeItem.name,
                currentMultiAz: universeItem.universeDetails.userIntent.isMultiAZ,
                azCheckState: universeItem.universeDetails.userIntent.isMultiAZ,
                regionSelected: universeItem.regions.map(function (item, idx) {
                  return {'value': item.uuid, 'name': item.name, "label": item.name}
                })
              })
            )
          } else {
              return null;
          }
        });
      }
      // If Edit Universe is called in the context of Current Universe
      else if(isValidObject(self.props.universe.currentUniverse)) {
        currentProvider = self.props.universe.currentUniverse.provider.uuid;
        currentMultiAz = self.props.universe.currentUniverse.universeDetails.userIntent.isMultiAZ;
        universeName = self.props.universe.currentUniverse.name;
        var items = self.props.universe.currentUniverse.regions.map(function (item, idx) {
          return {'value': item.uuid, 'name': item.name, "label": item.name}
        });
        self.props.getRegionListItems(currentProvider, currentMultiAz);
        self.props.getInstanceTypeListItems(currentProvider);
      }
      this.state = {
        readOnlyInput: "readonly",
        readOnlySelect: "disabled",
        universeName: universeName,
        azCheckState: currentMultiAz,
        regionSelected: items,
        buttonType: "universe-button btn btn-xs btn-info",
        buttonIcon: "fa fa-pencil"
      }
    }
    // If Create Universe is called
    else {
      this.state = {
        readOnlyInput: "",
        readOnlySelect: "",
        universeName: "",
        azCheckState: true,
        buttonType: "universe-button btn btn-default btn-lg bg-orange ",
        buttonIcon: "fa fa-database"
      }
    }
  }

  showModal() {
    this.setState({showModal: true});
  }

  closeModal() {
    this.setState({showModal: false});
  }

  providerChanged(event) {
    var providerUUID = event.target.value;
    var multiAZCheck = this.state.azCheckState;
    this.props.getRegionListItems(providerUUID, multiAZCheck);
    this.props.getInstanceTypeListItems(providerUUID);
  }

  multiAZChanged() {
    if (this.state.azCheckState === true) {
      this.setState({azCheckState: false});
    } else {
      this.setState({azCheckState: true});
    }
  }

  regionChanged(value){
    this.setState({regionSelected: value})
  }
  
  universeAction(event){
    event.preventDefault();
    var self = this;
    if (this.props.type === "Create") {
      self.props.createNewUniverse($('#universeForm').serialize());
    } else {
      var universeUUID = typeof self.props.uuid !== "undefined" ? self.props.uuid :
        this.props.universe.currentUniverse.universeUUID;
      self.props.editUniverse(universeUUID, $('#universeForm').serialize());
    }
    self.closeModal();
  }

  componentWillUnmount() {
    this.setState({
      readOnlyInput: "",
      readOnlySelect: "",
      universeName: "",
      azCheckState: true,
      buttonType: ""
    });
  }

  render(){

    var universeProviderList = this.props.cloud.providers.map(function(providerItem,idx) {
      return <option key={providerItem.uuid} value={providerItem.uuid}>
        {providerItem.name}
      </option>;
    });

    if (this.props.type === "Create") {
      universeProviderList.unshift(<option key="" value=""></option>);
    }

    var universeRegionList = this.props.cloud.regions.map(function (regionItem, idx) {
      return {value: regionItem.uuid, label: regionItem.name};
    });

    var universeInstanceTypeList =
      this.props.cloud.instanceTypes.map(function (instanceTypeItem, idx) {
        return <option key={instanceTypeItem.instanceTypeCode}
                       value={instanceTypeItem.instanceTypeCode}>
          {instanceTypeItem.instanceTypeCode}
        </option>
      });

    return (
      <div>
        <div>
          <div className={this.state.buttonType} onClick={this.showModal}>
            <i className={this.state.buttonIcon}></i>&nbsp;
            {this.props.type}
          </div>
        </div>
        <Modal show={this.state.showModal} onHide={this.closeModal}>
          <form id="universeForm" onSubmit={this.universeAction}>
            <Modal.Header closeButton>
              <Modal.Title>{this.props.type} Universe </Modal.Title>
            </Modal.Header>
            <Modal.Body>
              <label className="form-item-label">
                Universe Name
                <input type="text"
                       name="universeName"
                       className="form-control" readOnly={this.state.readOnlyInput}
                       defaultValue={this.state.universeName} onChange={this.universeNameChanged}
                      />
              </label>
              <label className="form-item-label">
                Cloud Provider
                <select name="provider" className="form-control"
                        disabled={this.state.readOnlySelect}
                        onChange={this.providerChanged}>
                        {universeProviderList}
                </select>
              </label>
              <label className="form-item-label" >
                Region<br/>
                <Select
                  name="regionList[]"
                  options={universeRegionList}
                  multi={this.state.azCheckState}
                  value={this.state.regionSelected}
                  onChange={this.regionChanged}
                />
              </label>
              <label className="form-item-label">
                <div>Number Of Nodes</div>
                <NumericInput className="form-control" min={3} max={32} value={3}/>
              </label>
              <div className="universeFormSplit">
                Advanced
              </div>
              <label className="form-item-label">
                Multi AZ Capable &nbsp;
                <input type="checkbox"
                       name="isMultiAZ"
                       id="isMultiAZ"
                       defaultChecked={this.state.azCheckState}
                       onChange={this.multiAZChanged}
                       value={this.state.azCheckState}
                      />
              </label>
              <label className="form-item-label">
                Instance Type
                <select name="instanceType" className="form-control">
                  {universeInstanceTypeList}
                </select>
              </label>
            </Modal.Body>
            <Modal.Footer>
              <button type="submit" className="btn btn-default btn-success btn-block" >
                {this.props.type} Universe
              </button>
            </Modal.Footer>
          </form>
        </Modal>
      </div>
    )
  }
}

export default UniverseModal;
