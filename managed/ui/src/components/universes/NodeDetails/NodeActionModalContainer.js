// Copyright YugaByte Inc.

import { NodeActionModal } from '../../universes';
import { connect } from 'react-redux';

import {
  getUniversePerNodeStatus,
  getUniversePerNodeStatusResponse,
  performUniverseNodeAction,
  performUniverseNodeActionResponse
} from '../../../actions/universe';

function mapStateToProps(state) {
  return {
    universe: state.universe
  };
}

const mapDispatchToProps = (dispatch) => {
  return {
    performUniverseNodeAction: (universeUUID, nodeName, actionType) => {
      return dispatch(performUniverseNodeAction(universeUUID, nodeName, actionType));
    },
    performUniverseNodeActionResponse: (payload) => {
      dispatch(performUniverseNodeActionResponse(payload));
    },
    preformGetUniversePerNodeStatus: (universeUUID) => {
      return dispatch(getUniversePerNodeStatus(universeUUID));
    },
    preformGetUniversePerNodeStatusResponse: (payload) => {
      dispatch(getUniversePerNodeStatusResponse(payload));
    }
  };
};

export default connect(mapStateToProps, mapDispatchToProps)(NodeActionModal);
