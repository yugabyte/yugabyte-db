// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { DropTable } from '../';
import { dropTable, dropTableResponse } from '../../../actions/tables';
import { fetchUniverseTasks, fetchUniverseTasksResponse } from '../../../actions/universe';

const mapDispatchToProps = (dispatch) => {
  return {
    dropTable: (universeUUID, tableUUID) => {
      dispatch(dropTable(universeUUID, tableUUID)).then((response) => {
        dispatch(dropTableResponse(response));
        dispatch(fetchUniverseTasks(universeUUID)).then((fetchResponse) => {
          dispatch(fetchUniverseTasksResponse(fetchResponse.payload));
        });
      });
    }
  };
};

const mapStateToProps = (state) => {
  return {
    currentTableDetail: state.tables.currentTableDetail
  };
};

export default connect(mapStateToProps, mapDispatchToProps)(DropTable);
