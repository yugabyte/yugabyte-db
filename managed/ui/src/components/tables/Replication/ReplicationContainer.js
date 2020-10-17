// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import Replication from './Replication';
import {
  queryMetrics,
  queryMetricsSuccess,
  queryMetricsFailure,
  resetMetrics
} from '../../../actions/graph';
import {
  createAlertDefinition, createAlertDefinitionResponse,
  getAlertDefinition,
  getAlertDefinitionResponse,
  getMasterLeader,
  getMasterLeaderResponse,
  resetMasterLeader, updateAlertDefinition, updateAlertDefinitionResponse
} from '../../../actions/universe';
import {reduxForm} from "redux-form";

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
    },
    getMasterLeader: (uuid) => {
      dispatch(getMasterLeader(uuid)).then((response) => {
        dispatch(getMasterLeaderResponse(response.payload));
      });
    },

    resetMasterLeader: () => {
      dispatch(resetMasterLeader());
    },

    createAlertDefinition: (uuid, data) => {
      dispatch(createAlertDefinition(uuid, data)).then((response) => {
        dispatch(createAlertDefinitionResponse(response.payload));
      });
    },

    getAlertDefinition: (uuid, name) => {
      dispatch(getAlertDefinition(uuid, name)).then((response) => {
        dispatch(getAlertDefinitionResponse(response.payload));
      });
    },

    updateAlertDefinition: (uuid, data) => {
      dispatch(updateAlertDefinition(uuid, data)).then((response) => {
        dispatch(updateAlertDefinitionResponse(response.payload));
      });
    }
  };
};

function mapStateToProps(state) {
  const { universe } = state;
  return {
    currentCustomer: state.customer.currentCustomer,
    alertDefinition: null,
    graph: state.graph,
    universe: universe,
    initialValues: {
      enableAlert: false,
      value: 180000
    }
  };
}

const replicationForm = reduxForm({
  form: 'replicationLagAlertForm',
  fields: ['enableAlert', 'value']
});

export default connect(mapStateToProps, mapDispatchToProps)(replicationForm(Replication));
