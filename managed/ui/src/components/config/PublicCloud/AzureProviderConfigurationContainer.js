// Copyright (c) YugaByte, Inc.

import { reduxForm } from 'redux-form';
import { connect } from 'react-redux';
import { AzureProviderConfiguration } from '../../config';

const mapStateToProps = (state) => {
  return {
    customer: state.customer,
    universe: state.universe,
    cloud: state.cloud
  };
};

const azureConfigForm = reduxForm({
  form: 'AzureConfigForm'
});


export default connect(mapStateToProps)(azureConfigForm(AzureProviderConfiguration));
