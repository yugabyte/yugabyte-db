// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import { connect } from 'react-redux';
import moment from 'moment';

import { YBButton } from '../../common/forms/fields';
import { getPromiseState } from '../../../utils/PromiseUtils';
import { AlertSnoozeModal } from '../../universes';

import {
  setAlertsConfig,
  setAlertsConfigResponse,
  fetchUniverseInfo,
  fetchUniverseInfoResponse
} from '../../../actions/universe';
import { RbacValidator } from '../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../redesign/features/rbac/ApiAndUserPermMapping';

class UniverseAction extends Component {
  constructor(props) {
    super(props);
    this.state = {
      showModal: false
    };
  }
  openModal = () => {
    this.setState((prevState, props) => {
      return {
        showModal: true
      };
    });
  };
  closeModal = () => {
    this.setState((prevState, props) => {
      return {
        showModal: false
      };
    });
  };
  UNSAFE_componentWillReceiveProps(nextProps) {
    if (
      getPromiseState(nextProps.backupState).isSuccess() ||
      getPromiseState(nextProps.alertsConfig).isSuccess()
    ) {
      this.closeModal();
      this.props.fetchCurrentUniverse(nextProps.universe.universeUUID);
    }
  }

  performAction = (values) => {
    const { universe, actionType, setAlertsConfig } = this.props;
    switch (actionType) {
      case 'alert-config':
        setAlertsConfig(universe.universeUUID, values);
        break;
      default:
        break;
    }
  };

  render() {
    const {
      btnClass,
      disabled,
      actionType,
      universe,
      universe: { universeConfig }
    } = this.props;

    const universePaused = universe?.universeDetails?.universePaused;
    let btnLabel = null;
    let btnIcon = null;
    let modalForm = null;
    switch (actionType) {
      case 'alert-config': {
        let disablePeriodSecs = null;
        let alertsSnoozed = false;
        if (universeConfig) {
          // If the disableAlertsUntilSecs is null or it is 0, it means the alerts is not snoozed.
          if (
            !universeConfig.disableAlertsUntilSecs ||
            universeConfig.disableAlertsUntilSecs === '0'
          ) {
            disablePeriodSecs = 0;
          } else {
            // If it has a value, we need to check if the snooze until time has elasped
            // if so then the universe alerts aren't snoozed.
            const disabledUntil = moment.unix(universeConfig.disableAlertsUntilSecs);
            if (disabledUntil.isValid()) {
              disablePeriodSecs = universeConfig.disableAlertsUntilSecs - moment().unix();
              alertsSnoozed = disablePeriodSecs > 0;
            } else {
              // If the disable alert until seconds is a invalid timestamp,
              // it means the alerts was snoozed indefinitely.
              alertsSnoozed = true;
            }
          }
        }
        btnLabel = alertsSnoozed ? 'Enable Alerts' : 'Snooze Alerts';
        btnIcon = alertsSnoozed ? 'fa fa-play' : 'fa fa-pause';
        modalForm = (
          <AlertSnoozeModal
            {...this.props}
            visible={this.state.showModal}
            onHide={this.closeModal}
            alertsSnoozed={alertsSnoozed}
            disablePeriodSecs={disablePeriodSecs}
            onFormSubmit={this.performAction}
          />
        );
        break;
      }
      default:
        break;
    }
    return (
      <div>
        {!universePaused && (
          <RbacValidator
            accessRequiredOn={{
              onResource: universe.universeUUID,
              ...ApiPermissionMap.MODIFY_UNIVERSE
            }}
            isControl
          >
            <YBButton
              btnText={btnLabel}
              btnIcon={btnIcon}
              btnClass={`btn ${btnClass}`}
              disabled={disabled}
              onClick={disabled ? null : this.openModal}
            />
          </RbacValidator>
        )}
        {modalForm}
      </div>
    );
  }
}

const mapDispatchToProps = (dispatch) => {
  return {
    setAlertsConfig: (uuid, payload) => {
      dispatch(setAlertsConfig(uuid, payload)).then((response) => {
        dispatch(setAlertsConfigResponse(response.payload));
      });
    },

    fetchCurrentUniverse: (universeUUID) => {
      dispatch(fetchUniverseInfo(universeUUID)).then((response) => {
        dispatch(fetchUniverseInfoResponse(response.payload));
      });
    }
  };
};

function mapStateToProps(state, ownProps) {
  return {
    alertsConfig: state.universe.alertsConfig,
    backupState: state.universe.backupState
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(UniverseAction);
