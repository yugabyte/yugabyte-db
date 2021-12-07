// Copyright YugaByte Inc.

import { connect } from 'react-redux';
import { withRouter } from 'react-router';

import { DeleteUniverse } from '../';
import {
  deleteUniverse,
  deleteUniverseResponse,
  deleteUniverseReadReplica,
  deleteUniverseReadReplicaResponse,
  resetUniverseInfo,
  fetchUniverseMetadata
} from '../../../actions/universe';

const mapDispatchToProps = (dispatch) => {
  return {
    submitDeleteUniverse: (uuid, isForceDelete, isDeleteBackups) => {
      dispatch(deleteUniverse(uuid, isForceDelete, isDeleteBackups)).then((response) => {
        dispatch(deleteUniverseResponse(response.payload));
      });
    },
    submitDeleteReadReplica: (clusterUUID, universeUUID, isForceDelete) => {
      dispatch(deleteUniverseReadReplica(clusterUUID, universeUUID, isForceDelete)).then(
        (response) => {
          dispatch(deleteUniverseReadReplicaResponse(response.payload));
        }
      );
    },
    resetUniverseInfo: () => {
      dispatch(resetUniverseInfo());
    },
    fetchUniverseMetadata: () => {
      dispatch(fetchUniverseMetadata());
    }
  };
};

function mapStateToProps(state) {
  return {
    universe: state.universe
  };
}

export default withRouter(connect(mapStateToProps, mapDispatchToProps)(DeleteUniverse));
