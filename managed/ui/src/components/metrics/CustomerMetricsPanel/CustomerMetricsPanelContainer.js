// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';

import { CustomerMetricsPanel } from '../../metrics';

const mapStateToProps = (state) => {
  return {
    customer: state.customer,
    universe: state.universe,
    visibleModal: state.modal.visibleModal,
    graph: state.graph
  };
};

export default connect(mapStateToProps)(CustomerMetricsPanel);
