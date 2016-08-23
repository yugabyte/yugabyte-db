//Copyright YugaByte Inc.

import UniverseDetail from '../components/UniverseDetail.js';
import { connect } from 'react-redux';
import {fetchUniverseInfo, fetchUniverseInfoSuccess, fetchUniverseInfoFailure, resetUniverseInfo } from '../actions/universe';

const mapDispatchToProps = (dispatch) => {
  return {
    getUniverseInfo: (uuid) => {
      dispatch(fetchUniverseInfo(uuid))
      .then((response) => {
        if (!response.error) {
          dispatch(fetchUniverseInfoSuccess(response.payload));
        } else {
          dispatch(fetchUniverseInfoFailure(response.payload));
        }
      });
    },
    resetUniverseInfo: () => {
      dispatch(resetUniverseInfo());
    }
  }
}

function mapStateToProps(state, ownProps) {
  return {
    universe: state.universe
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(UniverseDetail);
