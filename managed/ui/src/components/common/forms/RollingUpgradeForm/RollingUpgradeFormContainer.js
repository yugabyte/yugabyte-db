// Copyright (c) YugaByte, Inc.

import { reduxForm } from 'redux-form';
import { connect } from 'react-redux';
import { RollingUpgradeForm }  from '../../../common/forms';
import { rollingUpgrade, rollingUpgradeSuccess, rollingUpgradeFailure,
  closeDialog, resetRollingUpgrade } from '../../../../actions/universe';

const mapDispatchToProps = (dispatch) => {
  return {
    submitRollingUpgradeForm: (values, universeUUID) => {
      dispatch(rollingUpgrade(values, universeUUID)).then((response) => {
        if(response.payload.status !== 200) {
          dispatch(rollingUpgradeFailure(response.payload));
        } else {
          dispatch(closeDialog());
          dispatch(rollingUpgradeSuccess(response.payload));
        }
      })
    },
    resetRollingUpgrade: () => {
      dispatch(resetRollingUpgrade());
    }
  }
}

function mapStateToProps(state, ownProps) {

  return {
    universe: state.universe,
    softwareVersions: state.customer.softwareVersions
  };
}

var rollingUpgradeForm = reduxForm({
  form: 'RollingUpgradeForm'
})

export default connect(mapStateToProps, mapDispatchToProps)(rollingUpgradeForm(RollingUpgradeForm));
