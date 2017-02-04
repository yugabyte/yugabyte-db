// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { reduxForm } from 'redux-form';
import { CustomerMetricsPanel } from '../../metrics';
import { fetchUniverseList, fetchUniverseListSuccess, fetchUniverseListFailure }
         from '../../../actions/universe';

const mapDispatchToProps = (dispatch) => {
  return {
    fetchUniverseList: () => {
      dispatch(fetchUniverseList())
        .then((response) => {
          if (response.payload.status !== 200) {
            dispatch(fetchUniverseListFailure(response.payload));
          } else {
            dispatch(fetchUniverseListSuccess(response.payload));
          }
        });
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


export default connect( mapStateToProps, mapDispatchToProps)(graphPanelFilter(CustomerMetricsPanel));
