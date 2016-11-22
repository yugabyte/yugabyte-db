// Copyright (c) YugaByte, Inc.

import MetricsPanel from '../components/MetricsPanel.js';
import { queryCustomerMetrics, queryCustomerMetricsSuccess, queryCustomerMetricsFailure,
  queryUniverseMetrics, queryUniverseMetricsSuccess,
  queryUniverseMetricsFailure, resetMetrics } from '../actions/graph';
import { connect } from 'react-redux';

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
