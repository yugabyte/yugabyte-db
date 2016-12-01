// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';

import { fetchTaskProgress, fetchTaskProgressSuccess,
         fetchTaskProgressFailure, resetTaskProgress } from '../../actions/tasks';
import { TaskProgress } from '../../components/tasks';

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

export default connect( mapStateToProps, mapDispatchToProps)(TaskProgress);
