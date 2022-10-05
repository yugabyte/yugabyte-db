// Copyright YugaByte Inc.

import { connect } from 'react-redux';
import { ListTables } from '..';

function mapStateToProps(state, ownProps) {
  return {
    universe: state.universe,
    tables: state.tables,
    customer: state.customer,
    fetchUniverseTables: ownProps.fetchUniverseTables
  };
}

export default connect(mapStateToProps)(ListTables);
