// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { Modal } from 'react-bootstrap';
import YBButton from './YBButton';
import './stylesheets/YBModal.css'

export default class YBModal extends Component {

  render() {
    const {visible, onHide, size, formName, onFormSubmit, title, submitLabel,
      cancelLabel,  error, submitting, asyncValidating, footerAccessory,
      showCancelButton, className} = this.props;
    var btnDisabled = false;
    if (submitting || asyncValidating) {
      btnDisabled = true;
    }

    return (
      <Modal show={visible} onHide={onHide} bsSize={size} className={className}>
        <form name={formName} onSubmit={onFormSubmit}>
          <Modal.Header closeButton>
            <Modal.Title>{title}</Modal.Title>
            <div className={`yb-alert-item
                ${error ? '': 'hide'}`}>
              {error}
            </div>
          </Modal.Header>
          <Modal.Body>
            {this.props.children}
          </Modal.Body>
          {(footerAccessory || showCancelButton || onFormSubmit) &&
            <Modal.Footer>
              {footerAccessory && <div className="pull-left">{footerAccessory}</div>}
              {showCancelButton && <YBButton btnClass="btn" btnText={cancelLabel} onClick={onHide} />}
              {onFormSubmit && <YBButton btnClass="btn bg-orange" disabled={btnDisabled}
                btnText={submitLabel} onClick={onFormSubmit} />}
            </Modal.Footer>
          }
        </form>
      </Modal>
    )
  }
}

YBModal.propTypes = {
  title: PropTypes.string.isRequired,
  visible: PropTypes.bool,
  size: PropTypes.oneOf(['large', 'small', 'xsmall']),
  formName: PropTypes.string,
  onFormSubmit: PropTypes.func,
  submitLabel: PropTypes.string,
  cancelLabel: PropTypes.string,
  footerAccessory: PropTypes.object,
  showCancelButton: PropTypes.bool
};

YBModal.defaultProps = {
  visible: false,
  submitLabel: 'OK',
  cancelLabel: 'Cancel',
  showCancelButton: false
};
