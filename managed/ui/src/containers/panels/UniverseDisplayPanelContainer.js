// Copyright YugaByte Inc.

import UniverseDisplayPanel from '../../components/panels/UniverseDisplayPanel.js';
import { connect } from 'react-redux';

function mapStateToProps(state) {
  return {
    universe: state.universe
  };
}

export default connect(mapStateToProps)(UniverseDisplayPanel);

