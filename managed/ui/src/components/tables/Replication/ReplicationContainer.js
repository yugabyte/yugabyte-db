// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import Replication from './Replication';
import {
  queryMetrics,
  queryMetricsSuccess,
  queryMetricsFailure,
  resetMetrics
} from '../../../actions/graph';
import {
  fetchUniverseInfo,
  fetchUniverseInfoResponse,
  resetMasterLeader
} from '../../../actions/universe';

const mapDispatchToProps = (dispatch) => {
  return {
    queryMetrics: (queryParams, panelType) => {
      dispatch(queryMetrics(queryParams)).then((response) => {
        if (!response.error) {
          dispatch(queryMetricsSuccess(response.payload, panelType));
        } else {
          dispatch(queryMetricsFailure(response.payload, panelType));
        }
      });
    },
    resetMetrics: () => {
      dispatch(resetMetrics());
    },
    resetMasterLeader: () => {
      dispatch(resetMasterLeader());
    },
    fetchCurrentUniverse: (universeUUID) => {
      return dispatch(fetchUniverseInfo(universeUUID)).then((response) => {
        dispatch(fetchUniverseInfoResponse(response.payload));
      });
    }
  };
};

function mapStateToProps(state) {
  const { universe } = state;
  return {
    customer: state.customer,
    graph: state.graph,
    universe
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(Replication);
