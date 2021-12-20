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
import {closeDialog, openDialog} from "../../../actions/modal";

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
    showModal: (modalName) => {
      dispatch(openDialog(modalName));
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
