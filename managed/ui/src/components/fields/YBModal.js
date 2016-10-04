// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { Modal } from 'react-bootstrap';
import YBButton from './YBButton';
export default class YBModal extends Component {
  render() {
    const {visible, onHide, size, formName, onFormSubmit, title} = this.props;
    return (
      <Modal show={visible} onHide={onHide} bsSize={size}>
        <form name={formName} onSubmit={onFormSubmit}>
          <Modal.Header closeButton>
            <Modal.Title>{title}</Modal.Title>
          </Modal.Header>
          <Modal.Body>
            {this.props.children}
          </Modal.Body>
          <Modal.Footer>
            <YBButton btnClass="btn-block bg-orange" btnText={title} btnType="submit"/>
          </Modal.Footer>
        </form>
      </Modal>
  )
  }
}

YBModal.propTypes = {
  visible: PropTypes.bool,
  size: PropTypes.oneOf(['large', 'small', 'xsmall']),
  formName: PropTypes.string.isRequired,
  title: PropTypes.string.isRequired,
  onFormSubmit: PropTypes.func.isRequired
};

YBModal.defaultProps = { visible: false };
