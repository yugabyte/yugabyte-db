// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { TasksList } from '../../tasks';
import {
  abortTask,
  abortTaskResponse,
  fetchCustomerTasks,
  fetchCustomerTasksSuccess,
  fetchCustomerTasksFailure,
  resetCustomerTasks
} from '../../../actions/tasks';
import { openDialog, closeDialog } from '../../../actions/modal';

const mapDispatchToProps = (dispatch) => {
  return {
    fetchCustomerTasks: () => {
      dispatch(fetchCustomerTasks()).then((response) => {
        if (!response.error) {
          dispatch(fetchCustomerTasksSuccess(response.payload));
        } else {
          dispatch(fetchCustomerTasksFailure(response.payload));
        }
      });
    },
    resetCustomerTasks: () => {
      dispatch(resetCustomerTasks());
    },
    abortTask: (taskUUID) => {
      return dispatch(abortTask(taskUUID)).then((response) => {
        return dispatch(abortTaskResponse(response.payload));
      });
    },
    hideTaskAbortModal: () => {
      dispatch(closeDialog());
    },
    showTaskAbortModal: () => {
      dispatch(openDialog('confirmAbortTask'));
    }
  };
};

function mapStateToProps(state) {
  return {
    universe: state.universe,
    customer: state.customer,
    tasks: state.tasks,
    visibleModal: state.modal.visibleModal
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(TasksList);
