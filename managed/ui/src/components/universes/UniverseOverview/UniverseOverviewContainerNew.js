// Copyright YugaByte Inc.

import { UniverseOverviewNew } from '../../universes';
import { connect } from 'react-redux';
import { openDialog, closeDialog } from '../../../actions/modal';

const mapDispatchToProps = (dispatch) => {
  return {
    showUniverseOverviewMapModal: () => {
      dispatch(openDialog('universeOverviewMapModal'));
    },
    showDemoCommandModal: () => {
      dispatch(openDialog('universeOverviewDemoModal'));
    },
    closeModal: () => {
      dispatch(closeDialog());
    }
  };
};

function mapStateToProps(state) {
  return {
    currentCustomer: state.customer.currentCustomer,
    runtimeConfigs: state.customer.runtimeConfigs,
    alerts: state.customer.alerts,
    layout: state.customer.layout,
    tasks: state.tasks,
    modal: state.modal,
    tables: state.tables,
    featureFlags: state.featureFlags
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(UniverseOverviewNew);
