// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { Modal } from 'react-bootstrap';
import Select from 'react-select';
import 'react-select/dist/react-select.css';
import $ from 'jquery';

class UniverseModal extends Component {
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
    props.getProviderListItems();
  }

  componentDidMount() {
    var self = this;
    if (this.props.type === "Edit") {
      var currentProvider = self.props.universe.currentUniverse.provider.uuid;
      var currentMultiAz = self.props.universe.currentUniverse.universeDetails.userIntent.isMultiAZ;
      var items = self.props.universe.currentUniverse.regions.map(function(item, idx){
        return {'value': item.uuid, 'name': item.name, "label": item.name}
      });
      this.state = {
        readOnlyInput: "readonly",
        readOnlySelect: "disabled",
        universeName: self.props.universe.currentUniverse.name,
        azCheckState: currentMultiAz,
        regionSelected: items
      }

      self.props.getRegionListItems(currentProvider, currentMultiAz);
      self.props.getInstanceTypeListItems(currentProvider);
    } else {
      this.state = {
        readOnlyInput: "",
        readOnlySelect: "",
        universeName: "",
        azCheckState: true
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
      self.props.editUniverse(this.props.universe.currentUniverse.universeUUID, $('#universeForm').serialize());
    }
    self.closeModal();
  }

  componentWillUnmount() {
    this.setState({
      readOnlyInput: "",
      readOnlySelect: "",
      universeName: "",
      azCheckState: true
    })
  }
  render(){
    const { cloud: { providers, regions, instanceTypes}} = this.props;
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
          <div className="universeButton btn btn-default" onClick={this.showModal}>
            {this.props.type} Universe
          </div>
        </div>
        <Modal show={this.state.showModal} onHide={this.closeModal}>
          <form id="universeForm" onSubmit={this.universeAction}>
            <Modal.Header closeButton>
              <Modal.Title>Edit Universe </Modal.Title>
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
                <select name="provider" className="form-control" disabled={this.state.readOnlySelect}
                        onChange={this.providerChanged}>
                        {universeProviderList}
                </select>
              </label>
              <label className="form-item-label" >
                Region<br/>
                <Select
                  name="regionList[]"
                  options={universeRegionList}
                  multi={this.state.azCheckState} value={this.state.regionSelected}
                  onChange={this.regionChanged}
                />
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
                       onChange={this.multiAZChanged} value={this.state.azCheckState}
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
