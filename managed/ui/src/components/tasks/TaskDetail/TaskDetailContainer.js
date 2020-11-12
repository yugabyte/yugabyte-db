// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { TaskDetail } from '../../tasks';
import {
  fetchFailedSubTasks,
  fetchFailedSubTasksResponse,
  fetchTaskProgress,
  fetchTaskProgressResponse,
  retryTask,
  retryTaskResponse
} from '../../../actions/tasks';
import {
  fetchUniverseList,
  fetchUniverseListResponse
} from '../../../actions/universe';


const mapDispatchToProps = (dispatch) => {
  return {
    fetchFailedTaskDetail: (taskUUID) => {
      dispatch(fetchFailedSubTasks(taskUUID)).then((response) => {
        dispatch(fetchFailedSubTasksResponse(response));
      });
    },
    fetchCurrentTaskDetail: (taskUUID) => {
      dispatch(fetchTaskProgress(taskUUID)).then((response) => {
        dispatch(fetchTaskProgressResponse(response.payload));
      });
    },
    retryCurrentTask: (taskUUID) => {
      dispatch(retryTask(taskUUID)).then((response) => {
        dispatch(retryTaskResponse(response.payload));
      });
    },
    fetchUniverseList: () => {
      dispatch(fetchUniverseList()).then((response) => {
        dispatch(fetchUniverseListResponse(response.payload));
      });
    }
  };
};

function mapStateToProps(state, ownProps) {
  return {
    universe: state.universe,
    tasks: state.tasks
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(TaskDetail);
