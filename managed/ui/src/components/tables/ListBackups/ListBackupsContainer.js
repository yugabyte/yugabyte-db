// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { ListBackups } from '../../tables';
import { fetchUniverseBackups, fetchUniverseBackupsResponse, resetUniverseBackups } from '../../../actions/universe';

const mapDispatchToProps = (dispatch) => {
  return {
    fetchUniverseBackups: (universeUUID) => {
      dispatch(fetchUniverseBackups(universeUUID))
      .then((response) => {
        dispatch(fetchUniverseBackupsResponse(response.payload));
      });
    },
    resetUniverseBackups: () => {
      dispatch(resetUniverseBackups());
    }
  };
};

function mapStateToProps(state, ownProps) {
  return {
    universeBackupList: state.universe.universeBackupList
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(ListBackups);
