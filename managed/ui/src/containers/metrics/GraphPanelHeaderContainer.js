// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';

import { GraphPanelHeader } from '../../components/metrics';
import { changeGraphQueryPeriod, resetGraphQueryPeriod } from '../../actions/graph';

const mapDispatchToProps = (dispatch) => {
  return {
    changeGraphQueryPeriod: (filterParams) => {
      dispatch(changeGraphQueryPeriod(filterParams));
    },
    resetGraphQueryPeriod: () => {
      dispatch(resetGraphQueryPeriod());
    }
  }
}

function mapStateToProps(state, ownProps) {
  return {
    graph: state.graph
  };
}

export default connect( mapStateToProps, mapDispatchToProps)(GraphPanelHeader);
