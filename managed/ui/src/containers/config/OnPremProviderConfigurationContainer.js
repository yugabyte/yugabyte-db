// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import OnPremProviderConfiguration from '../../components/config/OnPremProviderConfiguration';
import { reduxForm, formValueSelector  } from 'redux-form';

const mapStateToProps = (state) => {
  const selector = formValueSelector('OnPremProviderConfigForm');
  return {
    cloud: state.cloud,
    formValues: selector(state, 'regions')
  };
}

var form = 'OnPremProviderConfigForm';
var fields = ['Regions[][]'];

const mapDispatchToProps = (dispatch) => {
  return {

  }
}

var onPremConfigForm = reduxForm({
  form: form,
  fields: fields
})


export default connect(mapStateToProps, mapDispatchToProps)(onPremConfigForm(OnPremProviderConfiguration));
