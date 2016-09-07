// Copyright YugaByte Inc.

import DeleteUniverse from '../components/DeleteUniverse.js';
import { connect } from 'react-redux';
import { deleteUniverse, deleteUniverseSuccess, deleteUniverseFailure, resetUniverseInfo } from '../actions/universe';
import { browserHistory } from 'react-router'

const mapDispatchToProps = (dispatch) => {
  return {
    deleteUniverse: (uuid) => {
      dispatch(deleteUniverse(uuid))
        .then((response) => {
          if (!response.error) {
            dispatch(deleteUniverseSuccess(response.payload));
            browserHistory.push('/universes')

          } else {
            dispatch(deleteUniverseFailure(response.payload));
          }
        });
    },
    resetUniverseInfo: () => {
      dispatch(resetUniverseInfo());
    }
  }
}

function mapStateToProps(state) {
  return {
    universe: state.universe
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(DeleteUniverse);
