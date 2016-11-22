// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import Dashboard from '../components/Dashboard.js';
import {fetchUniverseList, fetchUniverseListSuccess, fetchUniverseListFailure, resetUniverseList} from '../actions/universe';
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
    }
  }
}

const mapStateToProps = (state) => {
  return {
    customer: state.customer,
    universe: state.universe,
    cloud: state.cloud
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(Dashboard);
