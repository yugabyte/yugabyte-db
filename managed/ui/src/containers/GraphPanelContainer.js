// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import GraphPanel from '../components/GraphPanel.js';

const mapStateToProps = (state) => {
  return {
    graph: state.graph
  };
}

export default connect(mapStateToProps)(GraphPanel);
