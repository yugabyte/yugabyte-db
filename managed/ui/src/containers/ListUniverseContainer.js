import ListUniverse from '../components/ListUniverse.js';
import {fetchUniverseList, fetchUniverseListSuccess, fetchUniverseListFailure, resetUniverseList,
        openDialog, closeDialog} from '../actions/universe';
import { connect } from 'react-redux';

const mapDispatchToProps = (dispatch) => {
  return {
    fetchUniverseList: () => {
      dispatch(fetchUniverseList())
        .then((response) => {
          if (response.payload.status !== 200) {
            dispatch(fetchUniverseListFailure(response.payload));
          } else {
            dispatch(fetchUniverseListSuccess(response.payload));
          }
        });
    },
    resetUniverseList: () => {
      dispatch(resetUniverseList());
    },
    showUniverseModal: () => {
      dispatch(openDialog("universeModal"));
    },
    closeUniverseModal: () => {
      dispatch(closeDialog());
    }
  }
}

function mapStateToProps(state, ownProps) {
  return {
    universe: state.universe
  };
}

export default connect( mapStateToProps, mapDispatchToProps)(ListUniverse);
