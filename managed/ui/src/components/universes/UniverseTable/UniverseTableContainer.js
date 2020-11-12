// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';

import UniverseTable from './UniverseTable';
import { fetchUniverseMetadata, resetUniverseTasks } from '../../../actions/universe';
import {
  fetchUniversesPendingTasks,
  fetchUniversesPendingTasksSuccess,
  fetchUniversesPendingTasksFailure
} from '../../../actions/tasks';

const mapDispatchToProps = (dispatch) => {
  return {
    fetchUniverseMetadata: () => {
      dispatch(fetchUniverseMetadata());
    },

    fetchAllUniversesTasks: () => {
      dispatch(fetchUniversesPendingTasks()).then((response) => {
        if (!response.error) {
          dispatch(fetchUniversesPendingTasksSuccess(response.payload));
        } else {
          dispatch(fetchUniversesPendingTasksFailure(response.payload));
        }
      });
    },
    resetUniverseTasks: () => {
      dispatch(resetUniverseTasks());
    }
  };
};

function mapStateToProps(state) {
  return {
    universe: state.universe,
    customer: state.customer,
    universesPendingTasks: state.tasks.universesPendingTasks,
    providers: state.cloud.providers
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(UniverseTable);
