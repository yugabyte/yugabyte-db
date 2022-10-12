// Copyright (c) YugaByte, Inc.
// TODO: Entire file needs to be removed once Top K metrics is tested and integrated fully
import { connect } from 'react-redux';

import { GraphPanel } from '../../metrics';
import {
  queryMetrics,
  queryMetricsSuccess,
  queryMetricsFailure,
  resetMetrics
} from '../../../actions/graph';

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
    }
  };
};
const mapStateToProps = (state) => {
  return {
    insecureLoginToken: state.customer.INSECURE_apiToken,
    graph: state.graph,
    customer: state.customer
  };
};

export default connect(mapStateToProps, mapDispatchToProps)(GraphPanel);
