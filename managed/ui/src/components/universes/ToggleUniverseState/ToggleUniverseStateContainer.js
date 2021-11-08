import { connect } from 'react-redux';
import { withRouter } from 'react-router';

import { ToggleUniverseState } from '../';
import {
  fetchUniverseMetadata,
  pauseUniverse,
  pauseUniverseResponse,
  resetUniverseInfo,
  restartUniverse,
  restartUniverseResponse
} from '../../../actions/universe';

const mapDispatchToProps = (dispatch) => {
  return {
    submitPauseUniverse: (universeUUID) => {
      dispatch(pauseUniverse(universeUUID)).then((res) => {
        dispatch(pauseUniverseResponse(res.payload));
      });
    },
    submitRestartUniverse: (universeUUID) => {
      dispatch(restartUniverse(universeUUID)).then((res) => {
        dispatch(restartUniverseResponse(res.payload));
      });
    },
    resetUniverseInfo: () => {
      dispatch(resetUniverseInfo());
    },
    fetchUniverseMetadata: () => {
      dispatch(fetchUniverseMetadata());
    }
  };
};

const mapStateToProps = (state) => {
  return {
    universe: state.universe
  };
};
export default withRouter(connect(mapStateToProps, mapDispatchToProps)(ToggleUniverseState));
