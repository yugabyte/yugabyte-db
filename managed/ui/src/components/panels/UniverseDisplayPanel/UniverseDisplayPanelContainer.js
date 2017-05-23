// Copyright YugaByte Inc.

import { connect } from 'react-redux';

import { UniverseDisplayPanel } from '../../../components/panels';
import {openDialog, closeDialog} from '../../../actions/universe';

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

export default connect(null, mapDispatchToProps)(UniverseDisplayPanel);

