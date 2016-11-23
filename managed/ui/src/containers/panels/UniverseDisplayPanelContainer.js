// Copyright YugaByte Inc.

import UniverseDisplayPanel from '../../components/panels/UniverseDisplayPanel.js';
import { connect } from 'react-redux';
import {openDialog, closeDialog} from '../../actions/universe';

function mapStateToProps(state) {
  return {
    universe: state.universe
  };
}

const mapDispatchToProps = (dispatch) => {
  return {
    showUniverseModal: () => {
      dispatch(openDialog("universeModal"));
    },
    closeUniverseModal: () => {
      dispatch(closeDialog());
    }
  }
}

export default connect(mapStateToProps, mapDispatchToProps)(UniverseDisplayPanel);

