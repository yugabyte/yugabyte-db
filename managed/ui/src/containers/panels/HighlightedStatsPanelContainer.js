// Copyright YugaByte Inc.

import HighlightedStatsPanel from '../../components/panels/HighlightedStatsPanel.js';
import { connect } from 'react-redux';

function mapStateToProps(state) {
  return {
    universe: state.universe
  };
}

export default connect(mapStateToProps)(HighlightedStatsPanel);

