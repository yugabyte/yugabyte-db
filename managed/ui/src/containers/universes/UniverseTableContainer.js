// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';

import { UniverseTable } from '../../components/universes'
import { fetchUniverseList, fetchUniverseListSuccess,
  fetchUniverseListFailure, resetUniverseList, fetchUniverseTasks,
  fetchUniverseTasksSuccess, fetchUniverseTasksFailure,
  resetUniverseTasks} from '../../actions/universe';
import { fetchTaskProgress, fetchCurrentTaskListSuccess,
  fetchCurrentTaskListFailure, resetTaskProgress } from '../../actions/tasks';

const mapDispatchToProps = (dispatch) => {
  return {
    fetchUniverseList: () => {
      dispatch(fetchUniverseList())
        .then((response) => {
          if (response.payload.status !== 200) {
            dispatch(fetchUniverseListFailure(response.payload));
          } else {
            dispatch(fetchUniverseListSuccess(response.payload));
          }
        });
    },
    resetUniverseList: () => {
      dispatch(resetUniverseList());
    },
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
    },
    fetchCurrentTaskList: (taskUUIDList) => {
      taskUUIDList.forEach(function(taskUUIDObject, idx){
        var taskUUID = taskUUIDObject.id;
        dispatch(fetchTaskProgress(taskUUID))
          .then((response) => {
            if (!response.error) {
              var taskItem = response.payload.data;
              taskItem.universeUUID = taskUUIDObject.universe;
              taskItem.taskData = taskUUIDObject.data;
              taskItem.taskUUID = taskUUID;
              dispatch(fetchCurrentTaskListSuccess(taskItem));
            } else {
              dispatch(fetchCurrentTaskListFailure(response.payload));
            }
          });
      })
    },
    resetTaskProgress: () => {
      dispatch(resetTaskProgress());
    }
  }
}

function mapStateToProps(state, ownProps) {
  return {
    universe: state.universe
  };
}

export default connect( mapStateToProps, mapDispatchToProps)(UniverseTable);
