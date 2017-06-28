//Copyright YugaByte Inc.

import { connect } from 'react-redux';
import { UniverseDetail } from '../../universes';
import {fetchUniverseInfo, fetchUniverseInfoResponse, resetUniverseInfo,
        fetchUniverseTasks, fetchUniverseTasksResponse,
        resetUniverseTasks, openDialog, closeDialog } from '../../../actions/universe';
import {listAccessKeys, listAccessKeysResponse} from '../../../actions/cloud';

const mapDispatchToProps = (dispatch) => {
  return {
    getUniverseInfo: (uuid) => {
      dispatch(fetchUniverseInfo(uuid))
      .then((response) => {
        dispatch(fetchUniverseInfoResponse(response.payload));
      });
    },
    resetUniverseInfo: () => {
      dispatch(resetUniverseInfo());
    },
    fetchUniverseTasks: (uuid) => {
      dispatch(fetchUniverseTasks(uuid))
      .then((response) => {
        dispatch(fetchUniverseTasksResponse(response.payload));
      });
    },
    resetUniverseTasks: () => {
      dispatch(resetUniverseTasks());
    },
    showUniverseModal: () => {
      dispatch(openDialog("universeModal"));
    },
    showGFlagsModal: () => {
      dispatch(openDialog("gFlagsModal"));
    },
    showDeleteUniverseModal: () => {
      dispatch(openDialog("deleteUniverseModal"));
    },
    showSoftwareUpgradesModal: () => {
      dispatch(openDialog("softwareUpgradesModal"));
    },
    closeModal: () => {
      dispatch(closeDialog());
    },
    fetchAccessKeys: (providerUUID) => {
      dispatch(listAccessKeys(providerUUID)).then((response) => {
        dispatch(listAccessKeysResponse(response.payload));
      })
    }
  }
}

function mapStateToProps(state, ownProps) {
  return {
    universe: state.universe
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(UniverseDetail);
