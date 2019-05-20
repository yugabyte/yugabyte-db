// Copyright YugaByte Inc.

import { UniverseOverviewNew } from '../../universes';
import { connect } from 'react-redux';
import { openDialog, closeDialog } from '../../../actions/modal';

const mapDispatchToProps = (dispatch) => {
  return {
    closeModal: () => {
      dispatch(closeDialog());
    },

    showUniverseOverviewMapModal: () => {
      dispatch(openDialog("universeOverviewMapModal"));
    },
  };
};

function mapStateToProps(state) {
  return {
    currentCustomer: state.customer.currentCustomer,
    layout: state.customer.layout,
    tasks: state.tasks,
    modal: state.modal,
    tables: state.tables
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(UniverseOverviewNew);
