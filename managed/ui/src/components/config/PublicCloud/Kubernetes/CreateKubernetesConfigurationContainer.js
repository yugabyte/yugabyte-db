// Copyright (c) YugaByte, Inc.

import { reduxForm } from 'redux-form';
import { connect } from 'react-redux';
import CreateKubernetesConfiguration from './CreateKubernetesConfiguration';
import { isNonEmptyObject, isNonEmptyString } from 'utils/ObjectUtils';

import { createProvider, createProviderResponse,
         createRegion, createRegionResponse, fetchCloudMetadata,
         createZones, createZonesResponse,
         createInstanceType, createInstanceTypeResponse } from 'actions/cloud';

const mapStateToProps = (state) => {
  return {
    customer: state.customer,
    providers: state.cloud.providers,
  };
};

const mapDispatchToProps = (dispatch) => {
  return {
    createKubernetesProvider: (providerName, providerConfig, regionData, zoneData, instanceTypes) => {
      dispatch(createProvider("kubernetes", providerName, providerConfig)).then((response) => {
        dispatch(createProviderResponse(response.payload));
        if (response.payload.status === 200) {
          const providerUUID = response.payload.data.uuid;
          instanceTypes.forEach((instanceTypeData) => {
            console.log(instanceTypeData);
            dispatch(createInstanceType("kubernetes", providerUUID, instanceTypeData)).then((response) => {
              dispatch(createInstanceTypeResponse(response.payload));
            });
          });
          dispatch(createRegion(providerUUID, regionData)).then((response) => {
            dispatch(createRegionResponse(response.payload));
            if (response.payload.status === 200) {
              const region = response.payload.data;
              dispatch(createZones(providerUUID, region.uuid, zoneData)).then((response) => {
                dispatch(fetchCloudMetadata());
                dispatch(createZonesResponse(response.payload));
              });
            }
          });
        }
      });
    }
  };
};

const validate = (values) => {
  const errors = {};
  if (!isNonEmptyString(values.accountName)) {
    errors.accountName = 'Config Name is Required';
  }
  if (!isNonEmptyObject(values.kubeConfig)) {
    errors.kubeConfig = 'Kube Config is Required';
  }
  if (!isNonEmptyString(values.regionCode)) {
    errors.regionCode = 'Region is Required';
  }
  if (!isNonEmptyString(values.zoneLabel)) {
    errors.zoneLabel = 'Zone label is Required';
  }
  return errors;
};

const kubernetesConfigForm = reduxForm({
  form: 'kubernetesConfigForm',
  validate
});

export default connect(mapStateToProps, mapDispatchToProps)(kubernetesConfigForm(CreateKubernetesConfiguration));
