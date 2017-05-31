// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { reduxForm } from 'redux-form';
import {OnPremProviderAndAccessKey} from '../../../config';
import {setOnPremConfigData} from '../../../../actions/cloud';
import {isDefinedNotNull} from 'utils/ObjectUtils';

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

const validate = values => {
  const errors = {};
  if (!isDefinedNotNull(values.name)) {
    errors.name = 'Provider Name is Required';
  }
  if (!isDefinedNotNull(values.keyCode)) {
    errors.keyCode = 'Key Code is Required';
  }
  if (!isDefinedNotNull(values.privateKeyContent)) {
    errors.privateKeyContent = 'Private Key Content is Required';
  }
  return errors;
};

var onPremProviderConfigForm = reduxForm({
  form: 'onPremConfigForm',
  validate,
  destroyOnUnmount: false
});

export default connect(mapStateToProps, mapDispatchToProps)(onPremProviderConfigForm(OnPremProviderAndAccessKey));
