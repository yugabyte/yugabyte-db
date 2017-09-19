// Copyright (c) YugaByte, Inc.

import { reduxForm } from 'redux-form';
import { connect } from 'react-redux';
import { RollingUpgradeForm }  from '../../../common/forms';
import {isNonEmptyObject, isNonEmptyArray} from 'utils/ObjectUtils';
import { rollingUpgrade, rollingUpgradeResponse, closeDialog, resetRollingUpgrade } from '../../../../actions/universe';

const mapDispatchToProps = (dispatch) => {
  return {
    /**
     * Dispatch Rolling Upgrade/ Gflag restart to endpoint and handle response.
     * @param values form data payload
     * @param universeUUID UUID of the current Universe
     * @param reset function that sets the value of the form to pristine state
     */
    submitRollingUpgradeForm: (values, universeUUID, reset) => {
      dispatch(rollingUpgrade(values, universeUUID)).then((response) => {
        dispatch(rollingUpgradeResponse(response.payload));
        dispatch(closeDialog());
        // Reset the Rolling upgrade form fields to pristine state,
        // component may be called multiple times within the context of Universe Detail.
        reset();
      });
    },
    resetRollingUpgrade: () => {
      dispatch(resetRollingUpgrade());
    }
  };
};

function mapStateToProps(state, ownProps) {
  const {universe: {currentUniverse}} = state;
  let initalGFlagValues = {};
  if (isNonEmptyObject(currentUniverse) && currentUniverse.data.universeDetails.userIntent) {
    const currentGFlags = currentUniverse.data.universeDetails.userIntent.gflags;
    const gFlagList = Object.keys(currentGFlags).map(function(gFlagKey){
      return {name: gFlagKey, value: currentGFlags[gFlagKey]};
    });
    if (isNonEmptyArray(gFlagList)) {
      initalGFlagValues = {gflags: gFlagList};
    }
  }
  initalGFlagValues.timeDelay = 180;
  return {
    universe: state.universe,
    softwareVersions: state.customer.softwareVersions,
    initialValues: initalGFlagValues
  };
}

const rollingUpgradeForm = reduxForm({
  form: 'RollingUpgradeForm'
});

export default connect(mapStateToProps, mapDispatchToProps)(rollingUpgradeForm(RollingUpgradeForm));
