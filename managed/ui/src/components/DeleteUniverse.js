// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Modal } from 'react-bootstrap';
import 'react-bootstrap-multiselect/css/bootstrap-multiselect.css';
import $ from 'jquery';

export default class DeleteUniverse extends Component {

  constructor(props) {
    super(props);
    this.showModal = this.showModal.bind(this);
    this.closeModal = this.closeModal.bind(this);
    this.confirmDelete = this.confirmDelete.bind(this);
    this.state = {
      showModal: false
    };
  }
  
  showModal() {
    this.setState({showModal: true});
  }

  closeModal() {
    this.setState({showModal: false});
  }

  confirmDelete() {
    this.props.deleteUniverse(this.props.universe.currentUniverse.universeUUID);
  }

  render() {
    return (
      <div>
        <div className="btn btn-default" onClick={this.showModal}> Delete </div>
        <Modal show={this.state.showModal} onHide={this.closeModal}>
          Are you sure you want to continue ?<br/>
          <div className="btn btn-default " onClick={this.confirmDelete}> Yes </div>
          <div className="btn btn-default"> No </div>
        </Modal>
      </div>
    )
  }
}
