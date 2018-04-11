// Copyright YugaByte Inc.

import { connect } from 'react-redux';
import { ListTables } from '..';
import { toggleTableView } from '../../../actions/tables';

const mapDispatchToProps = (dispatch) => {
  return {
    showCreateTable: () => {
      dispatch(toggleTableView("create"));
    }
  };
};

function mapStateToProps(state) {
  return {
    universe: state.universe,
    tables: state.tables
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(ListTables);
