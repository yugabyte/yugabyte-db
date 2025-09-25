// Copyright YugabyteDB, Inc.

import { ClusterInfoPanel } from '../../panels';
import { connect } from 'react-redux';

function mapStateToProps(state) {
  return {
    providers: state.cloud.providers
  };
}

export default connect(mapStateToProps)(ClusterInfoPanel);
