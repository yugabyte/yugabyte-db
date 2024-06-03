// Copyright (c) YugaByte, Inc.

import React from 'react';
import { connect } from 'react-redux';
import { getAlerts, getAlertsSuccess, getAlertsFailure } from '../../../actions/customers';
import { fetchUniverseList, fetchUniverseListResponse } from '../../../actions/universe';
import { AlertListNew } from './AlertListNew';
import AlertsList from './AlertsList';

const mapDispatchToProps = (dispatch) => {
  return {
    getAlertsList: () => {
      dispatch(getAlerts()).then((response) => {
        if (response.payload.status === 200) {
          dispatch(getAlertsSuccess(response.payload));
        } else {
          dispatch(getAlertsFailure(response.payload));
        }
      });
    },
    fetchUniverseList: () => {
      return new Promise((resolve) => {
        dispatch(fetchUniverseList()).then((response) => {
          dispatch(fetchUniverseListResponse(response.payload));
          resolve(response.payload.data);
        });
      });
    }
  };
};

function mapStateToProps(state) {
  const {
    featureFlags: { test, released }
  } = state;
  return {
    customer: state.customer,
    universe: state.universe,
    enableNewAlertList: test.adminAlertsConfig || released.adminAlertsConfig
  };
}

class SwitchAlertList extends React.Component {
  render() {
    if (this.props.enableNewAlertList) {
      return <AlertListNew {...this.props} />;
    }
    return <AlertsList {...this.props} />;
  }
}

export default connect(mapStateToProps, mapDispatchToProps)(SwitchAlertList);
