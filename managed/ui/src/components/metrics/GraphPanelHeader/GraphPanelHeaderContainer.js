// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { reduxForm } from 'redux-form';
import { GraphPanelHeader } from '../../metrics';
import { changeGraphQueryPeriod, resetGraphQueryPeriod } from '../../../actions/graph';

const mapDispatchToProps = (dispatch) => {
  return {
    changeGraphQueryFilters: (filterParams) => {
      dispatch(changeGraphQueryPeriod(filterParams));
    },
    resetGraphQueryPeriod: () => {
      dispatch(resetGraphQueryPeriod());
    }
  }
}

function mapStateToProps(state, ownProps) {
  return {
    graph: state.graph,
    universe: state.universe
  };
}


var graphPanelFilter = reduxForm({
  form: 'GraphPanelFilterForm',
  fields: ["timeSelect", "universeSelect"]
})


export default connect( mapStateToProps, mapDispatchToProps)(graphPanelFilter(GraphPanelHeader));
