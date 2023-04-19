// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';

import { UniverseConsole } from '..';
import { fetchUniverseList, fetchUniverseListResponse } from '../../../actions/universe';

const mapDispatchToProps = (dispatch) => {
  return {
    fetchUniverseList: () => {
      dispatch(fetchUniverseList()).then((response) => {
        dispatch(fetchUniverseListResponse(response.payload));
      });
    }
  };
};

function mapStateToProps(state) {
  return {
    universe: state.universe,
    customer: state.customer
  };
}

export const UniverseConsoleContainer = connect(
  mapStateToProps,
  mapDispatchToProps
)(UniverseConsole);
