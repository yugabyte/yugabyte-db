// Copyright (c) YugaByte, Inc.
// 
// Author: Nishant Sharma(nishant.sharma@hashedin.com)

import React, { Component } from 'react';
import { YBModal, YBTextInput } from '../../common/forms/fields';
import 'react-bootstrap-multiselect/css/bootstrap-multiselect.css';

export default class ToggleUniverseState extends Component {
  constructor(props) {
    super(props);
    this.state = {
      universeName: false
    };
  }

  onChangeUniverseName = (value) => {
    this.setState({ universeName: value });
  };

  closeDeleteModal = () => {
    this.props.onHide();
  };

  getModalInfo = () => {
    const {
      body,
      universe: {
        currentUniverse: {
          data: { name }
        }
      }
    } = this.props;
    return (
      <div>
        {body}
        <br />
        <br />
        <label>Enter universe name to confirm:</label>
        <YBTextInput
          label="Confirm universe name:"
          placeHolder={name}
          input={{ onChange: this.onChangeUniverseName, onBlur: () => {} }}
        />
      </div>
    );
  }

  toggleUniverseStateConfirmation = () => {
    const {
      universePaused,
      universe: {
        currentUniverse: { data }
      }
    } = this.props;
    !universePaused
    ? this.props.submitRestartUniverse(data.universeUUID)
    : this.props.submitPauseUniverse(data.universeUUID);
  }

  render() {
    const {
      visible,
      title,
      error,
      onHide,
      universe: {
        currentUniverse: {
          data: { name }
        }
      }
    } = this.props;
    return (
      <YBModal
        visible={visible}
        formName={'toggleUniverseStateForm'}
        onHide={onHide}
        submitLabel={'Yes'}
        cancelLabel={'No'}
        showCancelButton={true}
        title={title + name}
        onFormSubmit={this.toggleUniverseStateConfirmation}
        error={error}
        asyncValidating={this.state.universeName !== name}
      >
        {this.getModalInfo()}
      </YBModal>
    );
  }
}