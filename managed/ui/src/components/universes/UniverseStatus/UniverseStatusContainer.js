// Copyright (c) YugabyteDB, Inc.

import { connect } from 'react-redux';

import UniverseStatus from './UniverseStatus';
import { fetchUniverseInfo, fetchUniverseInfoResponse } from '../../../actions/universe';
import { showTaskInDrawer } from '../../../actions/tasks';

const mapDispatchToProps = (dispatch) => {
  return {
    fetchCurrentUniverse: (universeUUID) => {
      dispatch(fetchUniverseInfo(universeUUID)).then((response) => {
        dispatch(fetchUniverseInfoResponse(response.payload));
      });
    },
    showTaskDetailsInDrawer: (taskUUID) => {
      dispatch(showTaskInDrawer(taskUUID));
    }
  };
};

function mapStateToProps(state) {
  return {
    tasks: state.tasks,
    runtimeConfigs: state.customer.runtimeConfigs,
    featureFlags: state.featureFlags
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(UniverseStatus);
