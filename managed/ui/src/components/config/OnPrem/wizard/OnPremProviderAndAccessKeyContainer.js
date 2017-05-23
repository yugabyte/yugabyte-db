// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { reduxForm } from 'redux-form';
import {OnPremProviderAndAccessKey} from '../../../config';
import {setOnPremConfigData} from '../../../../actions/cloud';

const mapDispatchToProps = (dispatch, ownProps) => {
  return {
    setOnPremProviderAndAccessKey: (formData) => {
      var formSubmitVals = {provider: {name: formData.name}, key: {code: formData.keyCode, privateKeyContent: formData.privateKeyContent}};
      dispatch(setOnPremConfigData(formSubmitVals));
      ownProps.nextPage();
      }
    }
  }

const mapStateToProps = (state) => {
  return {
    onPremJsonFormData: state.cloud.onPremJsonFormData
  };
}

var onPremProviderConfigForm = reduxForm({
  form: 'onPremProviderConfigForm',
  destroyOnUnmount: false
});

export default connect(mapStateToProps, mapDispatchToProps)(onPremProviderConfigForm(OnPremProviderAndAccessKey));
