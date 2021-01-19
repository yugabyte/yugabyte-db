// Copyright (c) YugaByte, Inc.
// 
// Author: Nishant Sharma(nishant.sharma@hashedin.com)

import React, { Component } from 'react';
import { YBModal, YBTextInput } from '../../common/forms/fields';
import 'react-bootstrap-multiselect/css/bootstrap-multiselect.css';

export default class PauseUniverse extends Component {
  constructor(props) {
    super(props);

    // Initial State.
    this.state = {
      universeName: false
    };
  }

  // This method is used to enable the delete button once the universe value is set.
  onChangeUniverseName = (value) => {
    this.setState({ universeName: value });
  };

  // This method will return the modal body.
  getPauseModal = () => {
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

  // This method is used to confirm whether the user needs to pause the universe or not.
  universePauseConfirmation = () => {
    const {
      universe: {
        currentUniverse: { data }
      }
    } = this.props;

    this.props.submitPauseUniverse(data.universeUUID);
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
        formName={'PauseUniverseForm'}
        onHide={onHide}
        submitLabel={'Yes'}
        cancelLabel={'No'}
        showCancelButton={true}
        title={title + name}
        onFormSubmit={this.universePauseConfirmation}
        error={error}
        asyncValidating={this.state.universeName !== name}
      >
        {this.getPauseModal()}
      </YBModal>
    );
  }
}