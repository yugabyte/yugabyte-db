// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import {AddHostDataForm} from '../../config';
import { reduxForm, formValueSelector  } from 'redux-form';
import {setOnPremConfigData} from '../../../actions/config';

const mapStateToProps = (state) => {
  const selector = formValueSelector('OnPremProviderConfigForm');
  return {
    cloud: state.cloud,
    config: state.config,
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
