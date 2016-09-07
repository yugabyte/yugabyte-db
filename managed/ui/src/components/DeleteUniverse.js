// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Modal } from 'react-bootstrap';
import 'react-bootstrap-multiselect/css/bootstrap-multiselect.css';

export default class DeleteUniverse extends Component {

  constructor(props) {
    super(props);
    this.showDeleteModal = this.showDeleteModal.bind(this);
    this.closeDeleteModal = this.closeDeleteModal.bind(this);
    this.confirmDelete = this.confirmDelete.bind(this);
    this.state = {
      deleteModalShown: false
    };
  }

  showDeleteModal() {
    this.setState({deleteModalShown: true});
  }

  closeDeleteModal() {
    this.setState({deleteModalShown: false});
  }

  confirmDelete() {
    
    this.props.deleteUniverse(this.props.uuid);
  }

  render() {
    return (
      <div>
        <div className="universe-button btn btn-xs btn-danger " onClick={this.showDeleteModal}>
          <i className='fa fa-trash-o' onClick={this.closeDeleteModal}></i>
            &nbsp;Delete
        </div>
        <Modal show={this.state.deleteModalShown} onHide={this.closeDeleteModal}>
          <Modal.Header closeButton>
            <Modal.Title>Delete Universe </Modal.Title>
          </Modal.Header>
          <Modal.Body>
            <div className="delete-title">Delete Universe ?</div>
              <div className="delete-btn-group">
                <div className="btn btn-sm btn-default" onClick={this.confirmDelete}>
                  Yes
                </div>
                <div className="btn btn-sm btn-default">
                  No
                </div>
              </div>
           </Modal.Body>
        </Modal>
      </div>
    )
  }
}
