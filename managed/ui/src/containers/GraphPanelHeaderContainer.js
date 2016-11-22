// Copyright (c) YugaByte, Inc.

import GraphPanelHeader from '../components/GraphPanelHeader.js';
import { changeGraphQueryPeriod, resetGraphQueryPeriod } from '../actions/graph';
import { connect } from 'react-redux';

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
