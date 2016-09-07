// Copyright (c) YugaByte, Inc.

import TaskProgressPanel from '../components/TaskProgressPanel.js';
import { fetchTaskProgress, fetchTaskProgressSuccess,
         fetchTaskProgressFailure, resetTaskProgress } from '../actions/tasks';
import { connect } from 'react-redux';

const mapDispatchToProps = (dispatch) => {
  return {
    fetchTaskProgress: (taskUUID) => {
      dispatch(fetchTaskProgress(taskUUID))
      .then((response) => {
        if (!response.error) {
          dispatch(fetchTaskProgressSuccess(response.payload));
        } else {
          dispatch(fetchTaskProgressFailure(response.payload));
        }
      });
    },
    resetTaskProgress: () => {
      dispatch(resetTaskProgress());
    }
  }
}

function mapStateToProps(state, ownProps) {
  return {
    tasks: state.tasks,
    universe: state.universe
  };
}

export default connect( mapStateToProps, mapDispatchToProps)(TaskProgressPanel);
