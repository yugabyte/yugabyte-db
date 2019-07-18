// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';

import { OverviewMetrics } from '../../metrics';
import { queryMetrics, queryMetricsSuccess, queryMetricsFailure,
  resetMetrics } from '../../../actions/graph';
import { isKubernetesUniverse as checkKubernetesUniverse } from 'utils/UniverseUtils';

const mapDispatchToProps = (dispatch) => {
  return {
    queryMetrics: (queryParams, panelType) => {
      dispatch(queryMetrics(queryParams))
      .then((response) => {
        if (!response.error) {
          dispatch(queryMetricsSuccess(response.payload, panelType));
        } else {
          dispatch(queryMetricsFailure(response.payload, panelType));
        }
      });
    },
    resetMetrics: () => {
      dispatch(resetMetrics());
    }
  };
};
const mapStateToProps = (state) => {
  const isKubernetesUniverse = checkKubernetesUniverse(state.universe.currentUniverse.data);
  return {
    isKubernetesUniverse,
    graph: state.graph,
  };
};

export default connect( mapStateToProps, mapDispatchToProps)(OverviewMetrics);
