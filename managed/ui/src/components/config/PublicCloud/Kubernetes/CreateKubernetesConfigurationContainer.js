// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import CreateKubernetesConfiguration from './CreateKubernetesConfiguration';

import { createProvider, createProviderResponse,
         createRegion, createRegionResponse, fetchCloudMetadata,
         createZones, createZonesResponse } from 'actions/cloud';

const mapStateToProps = (state) => {
  return {
    customer: state.customer,
    providers: state.cloud.providers
  };
};

const mapDispatchToProps = (dispatch) => {
  return {
    createKubernetesProvider: (providerName, providerConfig, regionData, zoneData) => {
      dispatch(createProvider("kubernetes", providerName, providerConfig)).then((response) => {
        dispatch(createProviderResponse(response.payload));
        if (response.payload.status === 200) {
          const providerUUID = response.payload.data.uuid;
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


export default connect(mapStateToProps, mapDispatchToProps)(CreateKubernetesConfiguration);
