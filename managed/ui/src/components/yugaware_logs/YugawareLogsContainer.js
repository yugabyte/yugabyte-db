// Copyright YugaByte Inc.

import { connect } from 'react-redux';
import { YugawareLogs } from '../yugaware_logs';
import { getLogs, getLogsSuccess, getLogsFailure, setLogsLoading } from '../../actions/customers';
import { fetchUniverseList, fetchUniverseListResponse } from '../../actions/universe';

const mapDispatchToProps = (dispatch) => {
  return {
    getLogs: (maxLines, regex, universe, startDate, endDate) => {
      dispatch(setLogsLoading());
      return dispatch(getLogs(maxLines, regex, universe, startDate, endDate)).then((response) => {
        if (response.payload.status !== 200) {
          dispatch(getLogsFailure(response.payload));
          const payload = response.payload;
          const error = payload.data ? payload.data.error : payload;
          console.error(error);
        } else {
          dispatch(getLogsSuccess(response.payload));
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
  return {
    currentCustomer: state.customer.currentCustomer,
    yugawareLogs: state.customer.yugaware_logs,
    logError: state.customer.yugawareLogError
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(YugawareLogs);
