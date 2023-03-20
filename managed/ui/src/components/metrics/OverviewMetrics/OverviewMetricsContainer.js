// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { OverviewMetrics } from '../../metrics';
import {
  queryMetrics,
  queryMetricsSuccess,
  queryMetricsFailure,
  queryMasterMetricsSuccess,
  queryMasterMetricsFailure,
  resetMetrics
} from '../../../actions/graph';

const mapDispatchToProps = (dispatch) => {
  return {
    queryMetrics: (queryParams, panelType, isMasterMetrics = false) => {
      dispatch(queryMetrics(queryParams)).then((response) => {
        if (!response.error) {
          isMasterMetrics
            ? dispatch(queryMasterMetricsSuccess(response.payload, panelType))
            : dispatch(queryMetricsSuccess(response.payload, panelType));
        } else {
          isMasterMetrics
            ? dispatch(queryMasterMetricsFailure(response.payload, panelType))
            : dispatch(queryMetricsFailure(response.payload, panelType));
        }
      });
    },
    resetMetrics: () => {
      dispatch(resetMetrics());
    }
  };
};
const mapStateToProps = (state) => {
  return {
    customer: state.customer,
    graph: state.graph
  };
};

export default connect(mapStateToProps, mapDispatchToProps)(OverviewMetrics);
