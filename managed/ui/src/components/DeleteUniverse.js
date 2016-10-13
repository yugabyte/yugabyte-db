// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import {Modal} from 'react-bootstrap';
import YBButton from '../components/fields/YBButton';
import 'react-bootstrap-multiselect/css/bootstrap-multiselect.css';

export default class DeleteUniverse extends Component {

  constructor(props) {
    super(props);
    this.closeDeleteModal = this.closeDeleteModal.bind(this);
    this.confirmDelete = this.confirmDelete.bind(this);
  }

  closeDeleteModal() {
    this.props.onHide();
  }

  confirmDelete() {
    this.props.onHide();
    this.props.deleteUniverse(this.props.universe.currentUniverse.universeUUID);
  }

  render() {

    const { visible, onHide, universe: {currentUniverse: {name}} } = this.props;
    
    return (
      <Modal show={visible} onHide={onHide}>
        <Modal.Header>
          <Modal.Title>Delete Universe: { name }</Modal.Title>
        </Modal.Header>
        <Modal.Body>
          Are you sure you want to delete the universe. You will lose all your data!
        </Modal.Body>
        <Modal.Footer>
          <YBButton onClick={this.closeDeleteModal} btnText="No"/>
          <YBButton btnStyle="primary" onClick={this.confirmDelete} btnText="Yes"/>
        </Modal.Footer>
      </Modal>
    )
  }
}
