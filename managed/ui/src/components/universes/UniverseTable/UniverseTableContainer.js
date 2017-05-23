// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';

import UniverseTable from './UniverseTable';
import { fetchUniverseMetadata, resetUniverseTasks, setUniverseMetrics} from '../../../actions/universe';
import { fetchCustomerTasks, fetchCustomerTasksSuccess, fetchCustomerTasksFailure } from '../../../actions/tasks';
import { queryMetrics } from '../../../actions/graph';

const mapDispatchToProps = (dispatch) => {
  return {
    fetchUniverseMetadata: () => {
      dispatch(fetchUniverseMetadata());
    },

    fetchUniverseTasks: () => {
      dispatch(fetchCustomerTasks())
      .then((response) => {
        if (!response.error) {
          dispatch(fetchCustomerTasksSuccess(response.payload));
        } else {
          dispatch(fetchCustomerTasksFailure(response.payload));
        }
      });
    },

    resetUniverseTasks: () => {
      dispatch(resetUniverseTasks());
    },

    universeReadWriteData: () => {
      var startTime  = Math.floor(Date.now() / 1000) - (12 * 60 * 60 );
      var endTime = Math.floor(Date.now() / 1000);
      var queryParams = {
        metrics: ["disk_iops_by_universe"],
        start: startTime,
        end: endTime
      }
      dispatch(queryMetrics(queryParams))
        .then((response) => {
          if (response.payload.status === 200) {
            dispatch(setUniverseMetrics(response.payload));
          }
        });
    }
  }
}

function mapStateToProps(state) {
  return {
    universe: state.universe,
    graph: state.graph,
    tasks: state.tasks
  };
}

export default connect( mapStateToProps, mapDispatchToProps)(UniverseTable);
