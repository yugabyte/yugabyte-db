// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Modal } from 'react-bootstrap';
var Multiselect = require('react-bootstrap-multiselect');
import 'react-bootstrap-multiselect/css/bootstrap-multiselect.css';
import $ from 'jquery';

export default class CreateUniverse extends Component {
  constructor(props) {
    super(props);
    this.showModal = this.showModal.bind(this);
    this.closeModal = this.closeModal.bind(this);
    this.providerListChanged = this.providerListChanged.bind(this);
    this.createNewUniverse = this.createNewUniverse.bind(this);
    this.multiAZChanged = this.multiAZChanged.bind(this);
    this.state = {
      showModal: false,
      selectedProvider: 'select',
      universeName: '',
      selectedRegions: [],
      isMultiAZCheck: true
    };
  }

  showModal(){
    this.setState({showModal: true});
    this.props.getProviderListItems();
  }
  closeModal(){
    this.setState({showModal: false});
  }

  multiAZChanged(){
    if(this.state.isMultiAZCheck){
      this.setState({isMultiAZCheck: false});
    }else{
      this.setState({isMultiAZCheck: true});
    }
    this.props.getRegionListItems(this.state.selectedProvider, this.state.isMultiAZCheck);
  }

  providerListChanged(event){
    var selectedProvider = event.target.value;
    this.setState({selectedProvider: selectedProvider});
    this.props.getRegionListItems(selectedProvider, this.state.isMultiAZCheck);
    this.props.getInstanceTypeListItems(selectedProvider);
  }

  createNewUniverse(event){
    event.preventDefault();
    this.props.createNewUniverse($('#createUniverseForm').serialize());
    this.setState({showModal: false});
  }

  render() {
    var universeProviderList = this.props.universeProviderList.map(function(providerItem,idx) {
      return <option key={providerItem.uuid} value={providerItem.uuid}>{providerItem.name}</option>;
    });
    var universeRegionList = this.props.universeRegionList.map(function (regionItem,idx) {
      return {value: regionItem.uuid, label: regionItem.name}
    });
    var universeInstanceTypeList = this.props.universeInstanceTypeList.map(function (instanceTypeItem, idx){
      return <option key={instanceTypeItem.instanceTypeCode} value={instanceTypeItem.instanceTypeCode}>{ instanceTypeItem.instanceTypeCode }</option>
    });

    return (
      <div>
        <div>
          <div className="createUniverseButton btn btn-default btn-success" onClick={this.showModal}>
            Create Universe
          </div>
        </div>
        <Modal show={this.state.showModal} onHide={this.closeModal}>
          <form id="createUniverseForm" onSubmit={this.createNewUniverse}>
            <Modal.Header closeButton>
              <Modal.Title>Create A New Universe</Modal.Title>
            </Modal.Header>
            <Modal.Body>
              <label className="form-item-label">
                Universe Name
                <input name="universeName" className="form-control" />
              </label>
              <label className="form-item-label">
                Cloud Provider
                <select name="provider" className="form-control"  onChange={this.providerListChanged} value={this.state.selectedProvider}>
                  <option value=""></option>
                  {universeProviderList}
                </select>
              </label>
              <label className="form-item-label" >
                Region<br/>
                <Multiselect name="regionList[]" className="region-select form-control" id="regionSelected" onChange={this.regionListChanged} data={universeRegionList} multiple/>
              </label>
              <div className="createUniverseFormSplit">
                Advanced
              </div>
              <label className="form-item-label">
                Multi AZ Capable &nbsp;
                <input type="checkbox" name="isMultiAZ" defaultChecked="true" onChange={this.multiAZChanged} value={this.state.isMultiAZCheck}/>
              </label>
              <label className="form-item-label">
                Instance Type
                <select name="instanceType" className="form-control" onChange={this.instanceTypeChanged} value={this.state.selectedInstanceType}>
                  {universeInstanceTypeList}
                </select>
              </label>
            </Modal.Body>
            <Modal.Footer>
              <button type="submit" className="btn btn-default btn-success btn-block" >Create Universe</button>
            </Modal.Footer>
          </form>
        </Modal>
      </div>
    )
  }
}
