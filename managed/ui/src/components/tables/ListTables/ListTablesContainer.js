// Copyright YugaByte Inc.

import { connect } from 'react-redux';
import { ListTables } from '..';

function mapStateToProps(state) {
  return {
    universe: state.universe,
    tables: state.tables
  };
}

export default connect(mapStateToProps)(ListTables);
