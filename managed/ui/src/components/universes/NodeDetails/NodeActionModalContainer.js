// Copyright YugaByte Inc.

import { NodeActionModal } from '../../universes';
import { connect } from 'react-redux';

import {
  getNodeDetails,
  getNodeDetailsResponse,
  performUniverseNodeAction,
  performUniverseNodeActionResponse,

} from '../../../actions/universe';

function mapStateToProps(state) {
  return {
    universe: state.universe
  };
}

const mapDispatchToProps = (dispatch) => {
  return {
    getNodeDetails: (universeUUID, nodeName) => {
      return dispatch(getNodeDetails(universeUUID, nodeName));
    },
    getNodeDetailsResponse: (payload) => {
      dispatch(getNodeDetailsResponse(payload));
    },
    performUniverseNodeAction: (universeUUID, nodeName, actionType) => {
      return dispatch(performUniverseNodeAction(universeUUID, nodeName, actionType));
    },
    performUniverseNodeActionResponse: (payload) => {
      dispatch(performUniverseNodeActionResponse(payload));
    }
  };
};

export default connect(mapStateToProps, mapDispatchToProps)(NodeActionModal);
