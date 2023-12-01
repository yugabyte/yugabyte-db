import { connect } from 'react-redux';
import { withRouter } from 'react-router';
import { toast } from 'react-toastify';
import { ToggleUniverseState } from '../';
import {
  fetchUniverseMetadata,
  pauseUniverse,
  pauseUniverseResponse,
  resetUniverseInfo,
  restartUniverse,
  restartUniverseResponse
} from '../../../actions/universe';
import { createErrorMessage } from '../../../utils/ObjectUtils';

const mapDispatchToProps = (dispatch) => {
  return {
    submitPauseUniverse: (universeUUID) => {
      dispatch(pauseUniverse(universeUUID)).then((res) => {
        if (res.error) {
          if (res.payload.status !== 200) {
            toast.error(createErrorMessage(res.payload));
          }
        } else {
          dispatch(pauseUniverseResponse(res.payload));
        }
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
