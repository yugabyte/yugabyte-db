// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { ListBackups } from '../../tables';
import { fetchUniverseList, fetchUniverseListResponse,
  fetchUniverseBackups, fetchUniverseBackupsResponse,
  resetUniverseBackups } from '../../../actions/universe';

const mapDispatchToProps = (dispatch) => {
  return {
    fetchUniverseBackups: (universeUUID) => {
      dispatch(fetchUniverseBackups(universeUUID))
      .then((response) => {
        dispatch(fetchUniverseBackupsResponse(response.payload));
      });
    },
    fetchUniverseList: () => {
      dispatch(fetchUniverseList())
      .then((response) => {
        dispatch(fetchUniverseListResponse(response.payload));
      });
    },
    resetUniverseBackups: () => {
      dispatch(resetUniverseBackups());
    }
  };
};

function mapStateToProps(state, ownProps) {
  const tableTypes = {};
  state.tables.universeTablesList.forEach((t) => {
    tableTypes[t.tableUUID] = t.tableType;
  });
  return {
    universeBackupList: state.universe.universeBackupList,
    universeTableTypes: tableTypes,
    currentCustomer: state.customer.currentCustomer
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(ListBackups);
