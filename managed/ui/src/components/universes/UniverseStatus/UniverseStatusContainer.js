// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';

import UniverseStatus from './UniverseStatus';
import { fetchUniverseInfo, fetchUniverseInfoResponse } from '../../../actions/universe';
import { retryTask, retryTaskResponse } from '../../../actions/tasks';

const mapDispatchToProps = (dispatch) => {
  return {
    fetchCurrentUniverse: (universeUUID) => {
      dispatch(fetchUniverseInfo(universeUUID)).then((response) => {
        dispatch(fetchUniverseInfoResponse(response.payload));
      });
    },
    retryCurrentTask: (taskUUID) => {
      return dispatch(retryTask(taskUUID)).then((response) => {
        return dispatch(retryTaskResponse(response.payload));
      });
    }
  };
};

function mapStateToProps(state) {
  return {
    tasks: state.tasks,
    runtimeConfigs: state.customer.runtimeConfigs
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(UniverseStatus);
