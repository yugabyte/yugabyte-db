// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { Modal } from 'react-bootstrap';
import YBButton from './YBButton';
import './stylesheets/YBModal.css'
import {isValidObject} from '../../../../utils/ObjectUtils';

export default class YBModal extends Component {

  render() {
    const {visible, onHide, size, formName, onFormSubmit, title, submitLabel, error, submitting, 
      asyncValidating, footerAccessory, showCancelButton} = this.props;
    var btnDisabled = false;
    if (submitting || asyncValidating) {
      btnDisabled = true;
    }
    return (
      <Modal show={visible} onHide={onHide} bsSize={size}>
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
          <Modal.Footer>
            {footerAccessory && <div className="pull-left">{footerAccessory}</div>}
            {showCancelButton && <YBButton btnClass="btn" btnText="Cancel" onClick={onHide} />}
            <YBButton btnClass="btn bg-orange" disabled={btnDisabled} btnText={submitLabel} btnType="submit" />
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

YBModal.defaultProps = { visible: false, submitLabel: 'OK' };
