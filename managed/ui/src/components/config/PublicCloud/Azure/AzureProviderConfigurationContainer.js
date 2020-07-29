// Copyright (c) YugaByte, Inc.

import { reduxForm } from 'redux-form';
import { connect } from 'react-redux';
import { AzureProviderConfiguration } from '../../../config';
import { createProvider, createProviderResponse, fetchCloudMetadata,
         bootstrapProvider, bootstrapProviderResponse } from '../../../../actions/cloud';

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

const mapDispatchToProps = (dispatch) => {
  return {
    createAzureProvider: (name, config, regionFormVals) => {
      dispatch(createProvider("azu", name.trim(), config)).then((response) => {
        dispatch(createProviderResponse(response.payload));
        if (response.payload.status === 200) {
          dispatch(fetchCloudMetadata());
          const providerUUID = response.payload.data.uuid;
          dispatch(bootstrapProvider(providerUUID, regionFormVals)).then((boostrapResponse) => {
            dispatch(bootstrapProviderResponse(boostrapResponse.payload));
          });
        }
      });
    }
  };
};

export default connect(mapStateToProps, mapDispatchToProps)(azureConfigForm(AzureProviderConfiguration));
