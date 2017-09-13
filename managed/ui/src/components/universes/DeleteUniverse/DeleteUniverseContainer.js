// Copyright YugaByte Inc.

import { connect } from 'react-redux';
import { DeleteUniverse } from '../';
import { deleteUniverse, deleteUniverseResponse,
         resetUniverseInfo, fetchUniverseMetadata} from '../../../actions/universe';

const mapDispatchToProps = (dispatch) => {
  return {
    deleteUniverse: (uuid, isForceDelete) => {
      dispatch(deleteUniverse(uuid, isForceDelete))
        .then((response) => {
          dispatch(deleteUniverseResponse(response.payload));
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

function mapStateToProps(state) {
  return {
    universe: state.universe
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(DeleteUniverse);
