// Copyright YugaByte Inc.

import { connect } from 'react-redux';
import { browserHistory } from 'react-router';
import { DeleteUniverse } from '../';
import { deleteUniverse, deleteUniverseSuccess, deleteUniverseFailure,
         resetUniverseInfo, fetchUniverseList,fetchUniverseListSuccess,
         fetchUniverseListFailure } from '../../../actions/universe';

const mapDispatchToProps = (dispatch) => {
  return {
    deleteUniverse: (uuid) => {
      dispatch(deleteUniverse(uuid))
        .then((response) => {
          if (!response.error) {
            dispatch(deleteUniverseSuccess(response.payload));
            dispatch(fetchUniverseList())
              .then((response) => {
                if (response.payload.status !== 200) {
                  dispatch(fetchUniverseListFailure(response.payload));
                } else {
                  dispatch(fetchUniverseListSuccess(response.payload));
                  browserHistory.push('/universes');
                }
              });
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
