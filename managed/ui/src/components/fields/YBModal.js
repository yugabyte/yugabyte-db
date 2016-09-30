// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Modal } from 'react-bootstrap';

export default class YBModal extends Component {
  render() {
    const {visible, onClose} = this.props;
    return (
      <Modal show={visible} onHide={onClose}>
        <Modal.Header closeButton>
          <Modal.Title>{this.props.type} Universe </Modal.Title>
        </Modal.Header>
        <Modal.Body>
          {this.props.children}
        </Modal.Body>
      </Modal>
  )
  }
}
