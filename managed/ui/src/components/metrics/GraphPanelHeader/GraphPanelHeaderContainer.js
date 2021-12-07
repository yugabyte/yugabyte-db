// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { reduxForm } from 'redux-form';
import { GraphPanelHeader } from '../../metrics';
import {
  changeGraphQueryPeriod,
  resetGraphQueryPeriod,
  togglePrometheusQuery
} from '../../../actions/graph';
import { fetchUniverseList, fetchUniverseListResponse } from '../../../actions/universe';

const mapDispatchToProps = (dispatch) => {
  return {
    fetchUniverseList: () => {
      dispatch(fetchUniverseList()).then((response) => {
        dispatch(fetchUniverseListResponse(response.payload));
      });
    },
    changeGraphQueryFilters: (filterParams) => {
      dispatch(changeGraphQueryPeriod(filterParams));
    },
    resetGraphQueryPeriod: () => {
      dispatch(resetGraphQueryPeriod());
    },
    togglePrometheusQuery: () => {
      dispatch(togglePrometheusQuery());
    }
  };
};

function mapStateToProps(state, ownProps) {
  return {
    graph: state.graph,
    universe: state.universe,
    prometheusQueryEnabled: state.graph.prometheusQueryEnabled,
    customer: state.customer
  };
}

const graphPanelFilter = reduxForm({
  form: 'GraphPanelFilterForm',
  fields: ['timeSelect', 'universeSelect']
});

export default connect(mapStateToProps, mapDispatchToProps)(graphPanelFilter(GraphPanelHeader));
