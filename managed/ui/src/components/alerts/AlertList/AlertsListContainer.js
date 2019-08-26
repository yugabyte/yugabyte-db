// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { AlertsList } from '../../alerts';
import { getAlerts, getAlertsSuccess, getAlertsFailure } from '../../../actions/customers';

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
    }
  };
};

function mapStateToProps(state, ownProps) {
  return {
    customer: state.customer
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(AlertsList);
