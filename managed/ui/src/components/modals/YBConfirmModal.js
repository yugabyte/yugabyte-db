// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';
import { YBModal } from '../common/forms/fields';

export default class YBConfirmModal extends Component {
  constructor(props) {
    super(props);
    this.submitConfirmModal = this.submitConfirmModal.bind(this);
  }

  static propTypes = {
    name: PropTypes.string.isRequired,
    title: PropTypes.string.isRequired,
    onConfirm: PropTypes.func.isRequired,
    confirmLabel: PropTypes.string,
    cancelLabel: PropTypes.string
  }

  static defaultProps = {
    confirmLabel: 'Confirm',
    cancelLabel: 'Cancel'
  };

  submitConfirmModal() {
    const { onConfirm, hideConfirmModal } = this.props;
    if (onConfirm) {
      onConfirm();
    }
    hideConfirmModal();
  }

  render() {
    const { name, title, confirmLabel, cancelLabel } = this.props;

    return (
      <div className={name} key={name}>
        <YBModal title={title}
                 visible={this.props.visibleModal === this.props.currentModal} onHide={this.props.hideConfirmModal}
                 showCancelButton={true} cancelLabel={cancelLabel}
                 submitLabel={confirmLabel} onFormSubmit={this.submitConfirmModal}>
          {this.props.children}
        </YBModal>
      </div>
    )
  }
}
