// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { TasksList } from '../../tasks';
import {
  fetchCustomerTasks,
  fetchCustomerTasksSuccess,
  fetchCustomerTasksFailure,
  resetCustomerTasks
} from '../../../actions/tasks';

const mapDispatchToProps = (dispatch) => {
  return {
    fetchCustomerTasks: (page, limit) => {
      return dispatch(fetchCustomerTasks(page, limit)).then((response) => {
        if (!response.error) {
          return dispatch(fetchCustomerTasksSuccess(response.payload));
        } else {
          return dispatch(fetchCustomerTasksFailure(response.payload));
        }
      });
    },
    resetCustomerTasks: () => {
      dispatch(resetCustomerTasks());
    }
  };
};

function mapStateToProps(state) {
  return {
    universe: state.universe,
    customer: state.customer,
    tasks: state.tasks
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(TasksList);
