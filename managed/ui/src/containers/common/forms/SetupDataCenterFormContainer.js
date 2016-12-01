// Copyright (c) YugaByte, Inc.

import { reduxForm } from 'redux-form';
import { connect } from 'react-redux';

import { SetupDataCenterForm }  from '../../../components/common/forms';

const mapDispatchToProps = (dispatch) => {
  return {
    submitDCConfig: (value) => {
      // Add Reducer Logic for Value
    }
  }
}

var setupDCForm = reduxForm({
  form: 'SetupDCForm'
})

module.exports = connect(mapDispatchToProps)(setupDCForm(SetupDataCenterForm));
