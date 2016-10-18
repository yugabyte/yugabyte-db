// Copyright (c) YugaByte, Inc.

import SetupDataCenterForm from '../../components/forms/SetupDataCenterForm';
import { reduxForm } from 'redux-form';
import { connect } from 'react-redux';

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
