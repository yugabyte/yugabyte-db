// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';

import { ListUniverse } from '../../universes';
import { openDialog, closeDialog} from '../../../actions/universe';

const mapDispatchToProps = (dispatch) => {
  return {
    showUniverseModal: () => {
      dispatch(openDialog("universeModal"));
    },
    closeUniverseModal: () => {
      dispatch(closeDialog());
    }
  };
};

function mapStateToProps(state, ownProps) {
  return {
    universe: state.universe
  };
}

export default connect( mapStateToProps, mapDispatchToProps)(ListUniverse);
