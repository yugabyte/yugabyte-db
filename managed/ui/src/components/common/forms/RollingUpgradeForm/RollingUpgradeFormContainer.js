// Copyright (c) YugaByte, Inc.

import { reduxForm } from 'redux-form';
import { connect } from 'react-redux';
import { RollingUpgradeForm }  from '../../../common/forms';
import {isNonEmptyObject, isNonEmptyArray} from 'utils/ObjectUtils';
import { rollingUpgrade, rollingUpgradeResponse, closeDialog, resetRollingUpgrade } from '../../../../actions/universe';

const mapDispatchToProps = (dispatch) => {
  return {
    submitRollingUpgradeForm: (values, universeUUID) => {
      dispatch(rollingUpgrade(values, universeUUID)).then((response) => {
        dispatch(rollingUpgradeResponse(response.payload));
        if(response.payload.status === 200) {
          dispatch(closeDialog());
        }
      });
    },
    resetRollingUpgrade: () => {
      dispatch(resetRollingUpgrade());
    }
  };
};

function mapStateToProps(state, ownProps) {
  const {universe: {currentUniverse}} = state;
  let initalGFlagValues = null;
  if (isNonEmptyObject(currentUniverse) && currentUniverse.data.universeDetails.userIntent) {
    const currentGFlags = currentUniverse.data.universeDetails.userIntent.gflags;
    const gFlagList = Object.keys(currentGFlags).map(function(gFlagKey){
      return {name: gFlagKey, value: currentGFlags[gFlagKey]};
    });
    if (isNonEmptyArray(gFlagList)) {
      initalGFlagValues = {gflags: gFlagList};
    }
  }
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
