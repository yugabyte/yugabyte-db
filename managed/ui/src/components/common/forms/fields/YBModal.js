// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import PropTypes from 'prop-types';
import { Modal } from 'react-bootstrap';
import YBButton from './YBButton';
import './stylesheets/YBModal.scss';

const ESC_KEY_CODE = 27;

export default class YBModal extends Component {
  handleKeyPressFunction = (event) => {
    const { onHide } = this.props;
    if (event.keyCode === ESC_KEY_CODE) {
      onHide(event);
    }
  };

  handleFormSubmit = (event) => {
    const { onFormSubmit } = this.props;
    if (onFormSubmit) {
      onFormSubmit(event);
    }
    event.preventDefault();
  };

  componentDidMount() {
    document.addEventListener('keydown', this.handleKeyPressFunction, false);
  }

  componentWillUnmount() {
    document.removeEventListener('keydown', this.handleKeyPressFunction, false);
  }

  render() {
    const {
      visible,
      onHide,
      size,
      formName,
      onFormSubmit,
      title,
      submitLabel,
      cancelLabel,
      error,
      submitting,
      asyncValidating,
      footerAccessory,
      showCancelButton,
      className,
      dialogClassName,
      normalizeFooter,
      disableSubmit,
      customHeader,
      formClassName,
      cancelBtnProps = {}
    } = this.props;
    let btnDisabled = false;
    if (submitting || asyncValidating || disableSubmit) {
      btnDisabled = true;
    }
    let footerButtonClass = '';
    if (normalizeFooter) {
      footerButtonClass = 'modal-action-buttons';
    }
    return (
      <Modal
        show={visible}
        onHide={onHide}
        bsSize={size}
        className={className}
        dialogClassName={dialogClassName}
      >
        <form className={formClassName} name={formName} onSubmit={this.handleFormSubmit}>
          {customHeader || (
            <Modal.Header closeButton>
              <Modal.Title>{title}</Modal.Title>
              <div
                className={`yb-alert-item
                ${error ? '' : 'hide'}`}
              >
                {error}
              </div>
            </Modal.Header>
          )}
          <Modal.Body>{this.props.children}</Modal.Body>
          {(footerAccessory || showCancelButton || onFormSubmit) && (
            <Modal.Footer>
              <div className={footerButtonClass}>
                {onFormSubmit && (
                  <YBButton
                    btnClass="btn btn-orange pull-right"
                    disabled={btnDisabled}
                    btnText={submitLabel}
                    onClick={this.handleFormSubmit}
                    btnType="submit"
                  />
                )}
                {showCancelButton && (
                  <YBButton
                    btnClass="btn"
                    btnText={cancelLabel}
                    onClick={onHide}
                    {...cancelBtnProps}
                  />
                )}
                {footerAccessory && (
                  <div className="pull-left modal-accessory">{footerAccessory}</div>
                )}
              </div>
            </Modal.Footer>
          )}
        </form>
      </Modal>
    );
  }
}

YBModal.propTypes = {
  title: PropTypes.oneOfType([PropTypes.string, PropTypes.object]).isRequired,
  visible: PropTypes.bool,
  size: PropTypes.oneOf(['large', 'small', 'xsmall']),
  formName: PropTypes.string,
  onFormSubmit: PropTypes.func,
  onHide: PropTypes.func,
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
