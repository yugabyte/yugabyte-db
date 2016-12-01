// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';

import { MetricsPanel } from '../../components/metrics';
import { queryCustomerMetrics, queryCustomerMetricsSuccess, queryCustomerMetricsFailure,
  queryUniverseMetrics, queryUniverseMetricsSuccess,
  queryUniverseMetricsFailure, resetMetrics } from '../../actions/graph';

const mapDispatchToProps = (dispatch) => {
  return {
    queryCustomerMetrics: (queryParams) => {
      dispatch(queryCustomerMetrics(queryParams))
      .then((response) => {
        if (!response.error) {
          dispatch(queryCustomerMetricsSuccess(response.payload));
        } else {
          dispatch(queryCustomerMetricsFailure(response.payload));
        }
      });
    },

    queryUniverseMetrics: (universeUUID, queryParams) => {
      dispatch(queryUniverseMetrics(universeUUID, queryParams))
      .then((response) => {
        if (!response.error) {
          dispatch(queryUniverseMetricsSuccess(response.payload));
        } else {
          dispatch(queryUniverseMetricsFailure(response.payload));
        }
      });
    },
    resetMetrics: () => {
      dispatch(resetMetrics())
    }
  }
}

function mapStateToProps(state, ownProps) {
  return {
    graph: state.graph
  };
}

export default connect( mapStateToProps, mapDispatchToProps )(MetricsPanel);
