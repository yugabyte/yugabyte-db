// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Modal } from 'react-bootstrap';

export default class Dashboard extends Component {
  constructor(props) {
    super(props);
    this.showModal = this.showModal.bind(this);
    this.closeModal = this.closeModal.bind(this);
    this.state = {
      showModal: false
    };
  }

  showModal(){
    this.setState({showModal:true});
  }
  closeModal(){
    this.setState({showModal: false});
  }
  render() {
    return (
      <div>
        <div>
          <div className="createUniverseButton btn btn-default btn-success" onClick={this.showModal}>
            Create Universe
          </div>
        </div>
        <Modal show={this.state.showModal} onHide={this.closeModal}>
          <Modal.Header closeButton>
            <Modal.Title>Create A New Universe</Modal.Title>
          </Modal.Header>
          <Modal.Body>
            <form>
              <label className="form-item-label">
                Universe Name
                <input className="form-control" placeholder="Universe Name"/>
              </label>
              <label className="form-item-label">
                Cloud Provider
                <select className="form-control"></select>
              </label>
              <label className="form-item-label">
                Region
                <select className="form-control"></select>
              </label>
              <div className="createUniverseFormSplit">
                Advanced
              </div>
              <label className="form-item-label">
                Multi AZ Capable &nbsp;
                <input type="checkbox"/>
              </label>
              <label className="form-item-label">
                Instance Type
                <select className="form-control"></select>
              </label>
            </form>
          </Modal.Body>
          <Modal.Footer>
            <div className="btn btn-default btn-success">Create Universe</div>
          </Modal.Footer>
        </Modal>
      </div>
    )
  }
}
