// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import UniverseStatus from './UniverseStatus';

function mapStateToProps(state) {
  return {
    universe: state.universe,
    graph: state.graph,
    tasks: state.tasks
  };
}

export default connect(mapStateToProps)(UniverseStatus);
