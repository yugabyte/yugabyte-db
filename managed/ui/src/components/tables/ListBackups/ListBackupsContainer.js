// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { ListBackups } from '../../tables';
import {
  fetchUniverseList,
  fetchUniverseListResponse,
  fetchUniverseBackups,
  fetchUniverseBackupsResponse,
  resetUniverseBackups
} from '../../../actions/universe';

const mapDispatchToProps = (dispatch) => {
  return {
    fetchUniverseBackups: (universeUUID) => {
      dispatch(fetchUniverseBackups(universeUUID)).then((response) => {
        dispatch(fetchUniverseBackupsResponse(response.payload));
      });
    },
    fetchUniverseList: () => {
      dispatch(fetchUniverseList()).then((response) => {
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
  const {
    customer: { configs }
  } = state;
  const storageConfigs = configs.data.filter((config) => config.type === 'STORAGE');
  state.tables.universeTablesList.forEach((t) => {
    if (t.tableType === 'YQL_TABLE_TYPE') {
      tableTypes[t.tableUUID] = 'YCQL';
    } else if (t.tableType === 'PGSQL_TABLE_TYPE') {
      tableTypes[t.tableUUID] = 'YSQL';
    } else {
      tableTypes[t.tableUUID] = 'YEDIS';
    }
  });
  return {
    universeBackupList: state.universe.universeBackupList,
    universeTableTypes: tableTypes,
    currentCustomer: state.customer.currentCustomer,
    storageConfigs
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(ListBackups);
