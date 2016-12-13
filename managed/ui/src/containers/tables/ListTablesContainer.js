// Copyright YugaByte Inc.

import { connect } from 'react-redux';

import { ListTables } from '../../components/tables';
import {fetchUniverseTables, fetchUniverseTablesSuccess, fetchUniverseTablesFailure} from '../../actions/tables';

const mapDispatchToProps = (dispatch) => {
  return {
    fetchUniverseTables: (universeUUID) => {
      dispatch(fetchUniverseTables(universeUUID)).then((response) => {
          if (response.payload.status !== 200) {
            dispatch(fetchUniverseTablesFailure(response.payload));
          } else {
            dispatch(fetchUniverseTablesSuccess(response.payload));
          }
      });
    }
  }
}

function mapStateToProps(state) {
  return {
    universe: state.universe,
    tables: state.tables
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(ListTables);
