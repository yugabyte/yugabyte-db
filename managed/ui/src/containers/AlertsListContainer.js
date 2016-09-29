// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import AlertsList from '../components/AlertsList';
import { fetchUniverseTasks, fetchUniverseTasksSuccess,
  fetchUniverseTasksFailure, resetUniverseTasks
} from '../actions/universe';

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

export default connect(mapStateToProps, mapDispatchToProps)(AlertsList);
