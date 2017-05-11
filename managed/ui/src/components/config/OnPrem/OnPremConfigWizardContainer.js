// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import {OnPremConfigWizard} from '../../config';
import { reduxForm, formValueSelector  } from 'redux-form';
import {setOnPremConfigData} from '../../../actions/cloud';

const mapStateToProps = (state) => {
  const selector = formValueSelector('OnPremProviderConfigForm');
  return {
    cloud: state.cloud,
    formValues: selector(state, 'regions')
  };
}
var form = 'OnPremProviderConfigForm';
const mapDispatchToProps = (dispatch) => {
  return {
    setOnPremJsonData: (formData) => {
      dispatch(setOnPremConfigData(formData))
    }
  }
}

var onPremConfigForm = reduxForm({
  form: form,
  fields: [
    'regions[].name',
    'regions[].zones[].name',
    'regions[].zones[].hosts[].name'
  ]
});

export default connect(mapStateToProps, mapDispatchToProps)(onPremConfigForm(OnPremConfigWizard));
