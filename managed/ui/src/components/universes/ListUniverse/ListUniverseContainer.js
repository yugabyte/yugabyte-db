// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';

import { ListUniverse } from '../../universes';
import { closeUniverseDialog } from '../../../actions/universe';
import { openDialog, closeDialog} from '../../../actions/modal';

const mapDispatchToProps = (dispatch) => {
  return {
    showUniverseModal: () => {
      dispatch(openDialog("universeModal"));
    },
    closeUniverseModal: () => {
      dispatch(closeDialog());
      dispatch(closeUniverseDialog());
    }
  };
};

function mapStateToProps(state, ownProps) {
  return {
    universe: state.universe
  };
}

export default connect( mapStateToProps, mapDispatchToProps)(ListUniverse);
