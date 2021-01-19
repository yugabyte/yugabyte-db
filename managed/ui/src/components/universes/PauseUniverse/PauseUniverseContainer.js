import { connect } from 'react-redux';
import { PauseUniverse } from '../';
import { pauseUniverse, pauseUniverseResponse } from '../../../actions/universe';

const mapDispatchToProps = (dispatch) => {
  return {
    submitPauseUniverse: (universeUUID) => {
      dispatch(pauseUniverse(universeUUID).then(res => {
        dispatch(pauseUniverseResponse(res.payload));
      }));
    }
  }
};

const mapStateToProps = (state) => {
  return {
    universe: state.universe
  };
};

export default connect(mapStateToProps, mapDispatchToProps)(PauseUniverse);