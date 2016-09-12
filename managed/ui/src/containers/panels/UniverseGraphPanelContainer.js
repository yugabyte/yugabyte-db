// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import UniverseGraphPanel from '../../components/panels/UniverseGraphPanel';

function mapStateToProps(state) {
  return {
    universe: state.universe
  };
}

export default connect(mapStateToProps)(UniverseGraphPanel);

