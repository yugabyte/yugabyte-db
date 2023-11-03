// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { reduxForm } from 'redux-form';
import { GraphPanelHeader } from '../../metrics';
import {
  changeGraphQueryPeriod,
  resetGraphQueryPeriod,
  togglePrometheusQuery,
  getGrafanaJson,
  resetMetrics,
  resetGraphFilter,
  setGraphFilter
} from '../../../actions/graph';
import { fetchUniverseList, fetchUniverseListResponse } from '../../../actions/universe';
import { closeDialog, openDialog } from '../../../actions/modal';

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
    },
    getGrafanaJson: getGrafanaJson,
    showModal: (modalName) => {
      dispatch(openDialog(modalName));
    },
    resetMetrics: () => {
      dispatch(resetMetrics());
    },
    resetGraphFilter: () => {
      dispatch(resetGraphFilter());
    },
    setGraphFilter: (filter) => {
      dispatch(setGraphFilter(filter));
    },
    closeModal: () => {
      dispatch(closeDialog());
    }
  };
};

function mapStateToProps(state, ownProps) {
  const {
    featureFlags: { test, released }
  } = state;

  return {
    graph: state.graph,
    universe: state.universe,
    prometheusQueryEnabled: state.graph.prometheusQueryEnabled,
    customer: state.customer,
    visibleModal: state.modal.visibleModal,
    enableNodeComparisonModal: test.enableNodeComparisonModal || released.enableNodeComparisonModal
  };
}

const graphPanelFilter = reduxForm({
  form: 'GraphPanelFilterForm',
  fields: ['timeSelect', 'universeSelect']
});

export default connect(mapStateToProps, mapDispatchToProps)(graphPanelFilter(GraphPanelHeader));
