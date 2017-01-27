// Copyright YugaByte Inc.

import { HighlightedStatsPanel } from '../../panels';
import { connect } from 'react-redux';

function mapStateToProps(state) {
  return {
    universe: state.universe
  };
}

export default connect(mapStateToProps)(HighlightedStatsPanel);

