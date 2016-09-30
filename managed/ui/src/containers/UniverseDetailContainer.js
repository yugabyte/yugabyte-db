//Copyright YugaByte Inc.

import UniverseDetail from '../components/UniverseDetail.js';
import { connect } from 'react-redux';
import {fetchUniverseInfo, fetchUniverseInfoSuccess,
        fetchUniverseInfoFailure, resetUniverseInfo,
        fetchUniverseTasks, fetchUniverseTasksSuccess,
        fetchUniverseTasksFailure, resetUniverseTasks,
        openDialog, closeDialog } from '../actions/universe';

const mapDispatchToProps = (dispatch) => {
  return {
    getUniverseInfo: (uuid) => {
      dispatch(fetchUniverseInfo(uuid))
      .then((response) => {
        if (!response.error) {
          dispatch(fetchUniverseInfoSuccess(response.payload));
        } else {
          dispatch(fetchUniverseInfoFailure(response.payload));
        }
      });
    },
    resetUniverseInfo: () => {
      dispatch(resetUniverseInfo());
    },
    fetchUniverseTasks: (uuid) => {
      dispatch(fetchUniverseTasks(uuid))
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
    showUniverseModal: () => {
      dispatch(openDialog("universeModal"));
    },
    closeUniverseModal: () => {
      dispatch(closeDialog());
    }
  }
}

function mapStateToProps(state, ownProps) {
  return {
    universe: state.universe
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(UniverseDetail);
