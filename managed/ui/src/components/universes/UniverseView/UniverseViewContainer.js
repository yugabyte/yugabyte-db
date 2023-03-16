// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';

import { UniverseView } from './UniverseView';
import { fetchUniverseMetadata, resetUniverseTasks } from '../../../actions/universe';
import {
  fetchCustomerTasks,
  fetchCustomerTasksSuccess,
  fetchCustomerTasksFailure
} from '../../../actions/tasks';
import {
  fetchRunTimeConfigs,
  fetchRunTimeConfigsResponse,
  DEFAULT_RUNTIME_GLOBAL_SCOPE
} from '../../../actions/customers';
import { openDialog, closeDialog } from '../../../actions/modal';

const mapDispatchToProps = (dispatch) => {
  return {
    fetchUniverseMetadata: () => {
      dispatch(fetchUniverseMetadata());
    },

    fetchUniverseTasks: () => {
      dispatch(fetchCustomerTasks()).then((response) => {
        if (!response.error) {
          dispatch(fetchCustomerTasksSuccess(response.payload));
        } else {
          dispatch(fetchCustomerTasksFailure(response.payload));
        }
      });
    },

    fetchGlobalRunTimeConfigs: () => {
      return dispatch(fetchRunTimeConfigs(DEFAULT_RUNTIME_GLOBAL_SCOPE, true)).then((response) =>
        dispatch(fetchRunTimeConfigsResponse(response.payload))
      );
    },

    resetUniverseTasks: () => {
      dispatch(resetUniverseTasks());
    },

    showToggleUniverseStateModal: () => {
      dispatch(openDialog('toggleUniverseStateForm'));
    },

    showDeleteUniverseModal: () => {
      dispatch(openDialog('deleteUniverseModal'));
    },

    closeModal: () => {
      dispatch(closeDialog());
    }
  };
};

function mapStateToProps(state) {
  return {
    universe: state.universe,
    customer: state.customer,
    graph: state.graph,
    tasks: state.tasks,
    providers: state.cloud.providers,
    featureFlags: state.featureFlags,
    modal: state.modal
  };
}

export const UniverseViewContainer = connect(mapStateToProps, mapDispatchToProps)(UniverseView);
