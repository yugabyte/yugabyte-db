// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { isObject } from 'lodash';
import { OnPremConfiguration } from '../../config';
import { createProvider, createProviderResponse, createInstanceType, createInstanceTypeResponse,
  createRegion, createRegionResponse, createZone, createZoneResponse, createNodeInstance,
  createNodeInstanceResponse, createAccessKey, createAccessKeyResponse, resetProviderBootstrap,
  fetchCloudMetadata, getProviderList, getProviderListResponse } from '../../../actions/cloud';
import { isNonEmptyArray } from 'utils/ObjectUtils';
import {reset} from 'redux-form';

const mapStateToProps = (state) => {
  return {
    cloud: state.cloud,
    configuredProviders: state.cloud.providers,
    configuredRegions: state.cloud.supportedRegionList,
    accessKeys: state.cloud.accessKeys,
    cloudBootstrap: state.cloud.bootstrap,
    onPremJsonFormData: state.cloud.onPremJsonFormData
  };
};

const mapDispatchToProps = (dispatch) => {
  return {
    onPremConfigSuccess: () => {
      dispatch(fetchCloudMetadata());
      dispatch(resetProviderBootstrap());
    },

    createOnPremAccessKeys: (providerUUID, regionsMap, config) => {
      if (isObject(config) && isNonEmptyArray(config.regions) && isObject(config.key)) {
        dispatch(createAccessKey(providerUUID, regionsMap[config.regions[0].code], config.key)).then((response) => {
          dispatch(createAccessKeyResponse(response.payload));
        });
      }
    },

    createOnPremInstanceTypes: (providerType, providerUUID, config) => {
      if (isObject(config) && isNonEmptyArray(config.instanceTypes)) {
        config.instanceTypes.forEach((type) => {
          dispatch(createInstanceType(providerType, providerUUID, type)).then((response) => {
            dispatch(createInstanceTypeResponse(response.payload));
          })
        })
      }
    },

    createOnPremProvider: (providerType, config) => {
      dispatch(createProvider(providerType, config.provider.name, null)).then((response) => {
        dispatch(createProviderResponse(response.payload));
      });
    },

    createOnPremRegions: (providerUUID, config) => {
      if (isObject(config) && isNonEmptyArray(config.regions)) {
        config.regions.forEach((region) => {
          let formValues = { "code": region.code, "hostVPCId": "", "name": region.code, "latitude": region.latitude, "longitude": region.longitude};
          dispatch(createRegion(providerUUID, formValues)).then((response) => {
            dispatch(createRegionResponse(response.payload));
          })
        })
      }
    },

    createOnPremZones: (providerUUID, regionsMap, config) => {
      if (isObject(config) && isNonEmptyArray(config.regions)) {
        config.regions.forEach((region) => {
          if (isObject(region) && isNonEmptyArray(region.zones)) {
            region.zones.forEach((zone) => {
              dispatch(createZone(providerUUID, regionsMap[region.code], zone)).then((response) => {
                dispatch(createZoneResponse(response.payload));
              })
            })
          }
        })
      }
    },

    createOnPremNodes: (zonesMap, config) => {
      if (isObject(config) && isNonEmptyArray(config.nodes)) {
        config.nodes.forEach((node) => {
          if (isObject(node)) {
            node.nodeName = "yb-" + node.zone + "-n" + config.nodes.indexOf(node).toString();
            dispatch(createNodeInstance(zonesMap[node.zone], node)).then((response) => {
              dispatch(createNodeInstanceResponse(response.payload));
            });
          }
        })
      }
    },
    resetConfigForm: () => {
      dispatch(reset("onPremConfigForm"));
    },
    fetchProviderList: () => {
      dispatch(getProviderList()).then((response) => {
        dispatch(getProviderListResponse(response.payload));
      })
    },

  }
};

export default connect(mapStateToProps, mapDispatchToProps)(OnPremConfiguration);
