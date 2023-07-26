// Copyright (c) YugaByte, Inc.
//
// Author: Nishant Sharma(nishant.sharma@hashedin.com)

import { Component } from 'react';
import { YBModal, YBTextInput } from '../../common/forms/fields';
import { getPromiseState } from '../../../utils/PromiseUtils';

import './ToggleUniverseState.scss';

class ToggleUniverseState extends Component {
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

  toggleUniverseStateConfirmation = () => {
    const {
      universePaused,
      universe: {
        currentUniverse: { data }
      },
      focusedUniverse = null
    } = this.props;
    const { universeUUID } = focusedUniverse ? focusedUniverse : data;

    this.props.onHide();
    universePaused
      ? this.props.submitRestartUniverse(universeUUID)
      : this.props.submitPauseUniverse(universeUUID);
  };

  componentDidUpdate(prevProps) {
    if (
      (getPromiseState(prevProps.universe.pauseUniverse).isLoading() &&
        getPromiseState(this.props.universe.pauseUniverse).isSuccess()) ||
      (getPromiseState(prevProps.universe.restartUniverse).isLoading() &&
        getPromiseState(this.props.universe.restartUniverse).isSuccess())
    ) {
      this.props.fetchUniverseMetadata();
      if (this.props.location.pathname !== '/universes') {
        window.location.reload();
      }
    }
  }

  render() {
    const {
      visible,
      title,
      error,
      onHide,
      universePaused,
      universe: {
        currentUniverse: { data }
      },
      focusedUniverse = null
    } = this.props;
    const { name } = focusedUniverse ? focusedUniverse : data;

    return (
      <YBModal
        visible={visible}
        formName="toggleUniverseStateForm"
        onHide={onHide}
        submitLabel="Yes"
        cancelLabel="No"
        showCancelButton={true}
        title={title + name}
        onFormSubmit={this.toggleUniverseStateConfirmation}
        error={error}
        asyncValidating={this.state.universeName !== name}
      >
        <div>
          <span>Are you sure you want to {!universePaused ? 'pause' : 'resume'} the universe?</span>
          <br />
          <br />
          {!universePaused && (
            <>
              <span>When your universe is paused:</span>
              <ul className="toggle-universe-list">
                <li>Reads and writes will be disabled.</li>
                <li>Any configured alerts and health checks will not be triggered.</li>
                <li>Scheduled backups will be stopped.</li>
                <li>Any changes to universe configuration are not allowed.</li>
                <li>For Azure universes IP-addresses of any instance can be changed on restart.</li>
                <li>
                  All data in the cluster will be saved and the cluster can be unpaused at any time.
                </li>
              </ul>
            </>
          )}
          <label>Enter universe name to confirm:</label>
          <YBTextInput
            label="Confirm universe name:"
            placeHolder={name}
            input={{ onChange: this.onChangeUniverseName }}
          />
        </div>
      </YBModal>
    );
  }
}
export { ToggleUniverseState };
