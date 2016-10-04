// Copyright (c) YugaByte, Inc.

import TaskAlerts from '../components/TaskAlerts';
import { fetchUniverseTasks, fetchUniverseTasksSuccess,
         fetchUniverseTasksFailure, resetUniverseTasks} from '../actions/universe';

import { connect } from 'react-redux';

const mapDispatchToProps = (dispatch) => {
  return {

    fetchUniverseTasks: () => {
      dispatch(fetchUniverseTasks())
        .then((response) => {
          if (!response.error) {
            dispatch(fetchUniverseTasksSuccess(response.payload));
          } else {
            dispatch(fetchUniverseTasksFailure(response.payload));
          }
        });
    },
    resetUniverseTasks: () => {
      dispatch(resetUniverseTasks());
    }
  }
}

function mapStateToProps(state, ownProps) {
  return {
    universe: state.universe
  };
}

export default connect( mapStateToProps, mapDispatchToProps)(TaskAlerts);
