// Copyright (c) YugaByte, Inc.

import { reduxForm } from 'redux-form';
import { connect } from 'react-redux';
import AzureProviderConfiguration from '../../components/config/AzureProviderConfiguration';

const mapStateToProps = (state) => {
  return {
    customer: state.customer,
    universe: state.universe,
    cloud: state.cloud
  };
}

var azureConfigForm = reduxForm({
  form: 'AzureConfigForm'
})


export default connect(mapStateToProps)(azureConfigForm(AzureProviderConfiguration));
