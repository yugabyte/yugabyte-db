// Copyright (c) YugaByte, Inc.

import GraphPanelHeader from '../components/GraphPanelHeader.js';
import { changeGraphFilter, resetGraphFilter } from '../actions/graph';
import { connect } from 'react-redux';

const mapDispatchToProps = (dispatch) => {
  return {
    updateGraphFilter: (filterParams) => {
      dispatch(changeGraphFilter(filterParams));
    },
    resetGraphFilter: () => {
      dispatch(resetGraphFilter());
    }
  }
}

function mapStateToProps(state, ownProps) {
  return {
    graph: state.graph
  };
}

export default connect( mapStateToProps, mapDispatchToProps)(GraphPanelHeader);
