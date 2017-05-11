// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { OnPremConfiguration } from '../../config';
import { createProvider, createProviderResponse, createInstanceType, createInstanceTypeResponse,
  createRegion, createRegionResponse, createZone,
  createZoneResponse, createNodeInstance, createNodeInstanceResponse, createAccessKey,
  createAccessKeyResponse, resetProviderBootstrap, fetchCloudMetadata }
  from '../../../actions/cloud';
import { isProperObject, isValidArray } from '../../../utils/ObjectUtils';

const mapStateToProps = (state) => {
  return {
    cloud: state.cloud, // the state populated by "reducer_cloud.js"
    cloudBootstrap: state.cloud.bootstrap
  };
};

const mapDispatchToProps = (dispatch) => {
  return {
    onPremConfigSuccess: () => {
      dispatch(fetchCloudMetadata());
      dispatch(resetProviderBootstrap());
    },

    createOnPremAccessKeys: (providerUUID, regionsMap, config) => {
      if (isProperObject(config) && isValidArray(config.regions) && isProperObject(config.key)) {
        config.regions.forEach((region) => {
          if (isProperObject(region)) {
            dispatch(createAccessKey(providerUUID, regionsMap[region.code], config.key)).then((response) => {
              dispatch(createAccessKeyResponse(response.payload));
            });
          }
        })
      }
    },

    createOnPremInstanceTypes: (providerUUID, config) => {
      if (isProperObject(config) && isValidArray(config.instanceTypes)) {
        config.instanceTypes.forEach((type) => {
          dispatch(createInstanceType("onprem", providerUUID, type)).then((response) => {
            dispatch(createInstanceTypeResponse(response.payload));
          })
        })
      }
    },

    createOnPremProvider: (config) => {
      dispatch(createProvider("onprem", config.provider.name, null)).then((response) => {
        dispatch(createProviderResponse(response.payload));
      });
    },

    createOnPremRegions: (providerUUID, config) => {
      if (isProperObject(config) && isValidArray(config.regions)) {
        config.regions.forEach((region) => {
          dispatch(createRegion(providerUUID, region.code, "")).then((response) => {
            dispatch(createRegionResponse(response.payload));
          })
        })
      }
    },

    createOnPremZones: (providerUUID, regionsMap, config) => {
      if (isProperObject(config) && isValidArray(config.regions)) {
        config.regions.forEach((region) => {
          if (isProperObject(region) && isValidArray(region.zones)) {
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
      if (isProperObject(config) && isValidArray(config.nodes)) {
        config.nodes.forEach((node) => {
          if (isProperObject(node)) {
            node.nodeName = "yb-" + node.zone + "-n" + config.nodes.indexOf(node).toString();
            dispatch(createNodeInstance(zonesMap[node.zone], node)).then((response) => {
              dispatch(createNodeInstanceResponse(response.payload));
            });
          }
        })
      }
    }
  }
};

export default connect(mapStateToProps, mapDispatchToProps)(OnPremConfiguration);
