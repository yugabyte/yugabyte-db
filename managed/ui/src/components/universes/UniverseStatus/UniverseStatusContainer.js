// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import UniverseStatus from './UniverseStatus';
import { fetchUniverseInfo, fetchUniverseInfoResponse } from '../../../actions/universe';

const mapDispatchToProps = (dispatch) => {
  return {
    fetchCurrentUniverse: (universeUUID) => {
      dispatch(fetchUniverseInfo(universeUUID)).then((response) => {
        dispatch(fetchUniverseInfoResponse(response.payload));
      });
    }
  };
};


function mapStateToProps(state) {
  return {
    universe: state.universe,
    graph: state.graph,
    tasks: state.tasks
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(UniverseStatus);
