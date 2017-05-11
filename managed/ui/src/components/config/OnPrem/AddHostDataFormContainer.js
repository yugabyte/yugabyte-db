// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import {AddHostDataForm} from '../../config';
import { reduxForm, formValueSelector  } from 'redux-form';
import {setOnPremConfigData} from '../../../actions/cloud';

const mapStateToProps = (state) => {
  const selector = formValueSelector('OnPremProviderConfigForm');
  return {
    cloud: state.cloud,
    formValues: selector(state, 'regions')
  };
}
var form = 'AdditionalHostDataForm';
const mapDispatchToProps = (dispatch) => {
  return {
    setOnPremJsonData: (formData) => {
      dispatch(setOnPremConfigData(formData))
    }
  }
}

var addHostDataForm = reduxForm({
  form: form,
  fields: [
    'privateKey', 'rootUserName', 'directoryPath'
  ]
});

export default connect(mapStateToProps, mapDispatchToProps)(addHostDataForm(AddHostDataForm));
