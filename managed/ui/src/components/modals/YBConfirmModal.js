// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { YBModal, YBButton } from '../common/forms/fields';

export default class YBConfirmModal extends Component {
  static propTypes = {
    name: PropTypes.string.isRequired,
    btnLabel: PropTypes.string.isRequired,
    btnClass: PropTypes.string,
    title: PropTypes.string.isRequired,
    onConfirm: PropTypes.func.isRequired,
    confirmLabel: PropTypes.string,
    cancelLabel: PropTypes.string
  }

  static defaultProps = {
    confirmLabel: 'Confirm',
    cancelLabel: 'Cancel'
  };

  constructor(props) {
    super(props);
    this.state = { showConfirmModal: false };
    this.toggleConfirmModal = this.toggleConfirmModal.bind(this);
    this.submitConfirmModal = this.submitConfirmModal.bind(this);

  }

  toggleConfirmModal() {
    this.setState({showConfirmModal: !this.state.showConfirmModal});
  }

  submitConfirmModal() {
    const { onConfirm } = this.props;
    if (onConfirm) {
      onConfirm();
    }
    this.toggleConfirmModal();
  }

  render() {
    const { name, title, disabled, btnLabel, btnClass, confirmLabel, cancelLabel } = this.props;

    return (
      <div className={name} key={name}>
        <YBButton btnText={btnLabel} disabled={disabled}
          btnClass={btnClass} onClick={this.toggleConfirmModal}/>
        <YBModal title={title}
                 visible={this.state.showConfirmModal} onHide={this.toggleConfirmModal}
                 showCancelButton={true} cancelLabel={cancelLabel}
                 submitLabel={confirmLabel} onFormSubmit={this.submitConfirmModal}>
          {this.props.children}
        </YBModal>
      </div>
    )
  }
}
