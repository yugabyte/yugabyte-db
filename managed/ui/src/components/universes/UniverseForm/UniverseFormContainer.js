// Copyright (c) YugaByte, Inc.

import { reduxForm, formValueSelector } from 'redux-form';
import { connect } from 'react-redux';
import { fetchCustomerTasks, fetchCustomerTasksSuccess, fetchCustomerTasksFailure } from '../../../actions/tasks';
import UniverseForm from './UniverseForm';
import { getInstanceTypeList, getRegionList, getRegionListResponse, getInstanceTypeListResponse,
         getNodeInstancesForProvider, getNodesInstancesForProviderResponse, getSuggestedSpotPrice,
         getSuggestedSpotPriceResponse, resetSuggestedSpotPrice } from '../../../actions/cloud';
import { createUniverse, createUniverseResponse, editUniverse, editUniverseResponse, closeDialog,
         configureUniverseTemplate, configureUniverseTemplateResponse, configureUniverseTemplateSuccess,
         configureUniverseResources, configureUniverseResourcesResponse,
         checkIfUniverseExists, setPlacementStatus, resetUniverseConfiguration,
         fetchUniverseInfo, fetchUniverseInfoResponse, fetchUniverseMetadata, fetchUniverseTasks,
         fetchUniverseTasksResponse } from '../../../actions/universe';
import { isDefinedNotNull, isNonEmptyObject, isNonEmptyString, normalizeToPositiveFloat }
  from '../../../utils/ObjectUtils';
import { IN_DEVELOPMENT_MODE } from '../../../config';
import { getPrimaryCluster } from '../../../utils/UniverseUtils';

const mapDispatchToProps = (dispatch) => {
  return {
    submitConfigureUniverse: (values) => {
      dispatch(configureUniverseTemplate(values)).then((response) => {
        dispatch(configureUniverseTemplateResponse(response.payload));
      });
    },

    fetchUniverseResources: (payload) => {
      dispatch(configureUniverseResources(payload)).then((resourceData) => {
        dispatch(configureUniverseResourcesResponse(resourceData.payload));
      });
    },

    submitCreateUniverse: (values) => {
      dispatch(createUniverse(values)).then((response) => {
        dispatch(createUniverseResponse(response.payload));
      });
    },

    fetchCustomerTasks: () => {
      dispatch(fetchCustomerTasks()).then((response) => {
        if (!response.error) {
          dispatch(fetchCustomerTasksSuccess(response.payload));
        } else {
          dispatch(fetchCustomerTasksFailure(response.payload));
        }
      });
    },

    fetchUniverseMetadata: () => {
      dispatch(fetchUniverseMetadata());
    },

    fetchCurrentUniverse: (universeUUID) => {
      dispatch(fetchUniverseInfo(universeUUID)).then((response) => {
        dispatch(fetchUniverseInfoResponse(response.payload));
      });
    },

    closeUniverseDialog: () => {
      dispatch(closeDialog());
    },

    submitEditUniverse: (values, universeUUID) => {
      dispatch(editUniverse(values, universeUUID)).then((response) => {
        dispatch(editUniverseResponse(response.payload));
      });
    },

    getInstanceTypeListItems: (provider) => {
      dispatch(getInstanceTypeList(provider)).then((response) => {
        dispatch(getInstanceTypeListResponse(response.payload));
      });
    },

    getRegionListItems: (provider) => {
      dispatch(getRegionList(provider)).then((response) => {
        dispatch(getRegionListResponse(response.payload));
      });
    },

    getSuggestedSpotPrice: (providerUUID, instanceType, regions) => {
      dispatch(getSuggestedSpotPrice(providerUUID, instanceType, regions)).then((response) => {
        dispatch(getSuggestedSpotPriceResponse(response.payload));
      });
    },

    resetSuggestedSpotPrice: () => {
      dispatch(resetSuggestedSpotPrice());
    },

    setPlacementStatus: (currentStatus) => {
      dispatch(setPlacementStatus(currentStatus));
    },

    resetConfig: () => {
      dispatch(resetUniverseConfiguration());
    },

    fetchUniverseTasks: (universeUUID) => {
      dispatch(fetchUniverseTasks(universeUUID)).then((response) => {
        dispatch(fetchUniverseTasksResponse(response.payload));
      });
    },

    getExistingUniverseConfiguration: (universeDetail) => {
      dispatch(configureUniverseTemplateSuccess({data: universeDetail}));
      dispatch(configureUniverseResources(universeDetail)).then((resourceData) => {
        dispatch(configureUniverseResourcesResponse(resourceData.payload));
      });
    },

    fetchNodeInstanceList: (providerUUID) => {
      dispatch(getNodeInstancesForProvider(providerUUID)).then((response) => {
        dispatch(getNodesInstancesForProviderResponse(response.payload));
      });
    }
  };
};

const formFieldNames =
  ['formType', 'primary.universeName', 'primary.provider', 'primary.providerType', 'primary.regionList',
    'primary.numNodes', 'primary.instanceType', 'primary.ybSoftwareVersion', 'primary.accessKeyCode',
    'primary.masterGFlags', 'primary.tserverGFlags', 'primary.spotPrice', 'primary.diskIops', 'primary.numVolumes',
    'primary.volumeSize', 'primary.ebsType', 'primary.assignPublicIP',
    'async.provider', 'async.providerType', 'async.spotPrice', 'async.regionList', 'async.numNodes',
    'async.instanceType', 'async.ybSoftwareVersion', 'async.accessKeyCode', 'async.assignPublicIP',
    'spotPrice', 'useSpotPrice', 'masterGFlags', 'tserverGFlags', 'asyncClusters'];

function mapStateToProps(state, ownProps) {
  const {universe: { currentUniverse }} = state;
  const data = {
    "primary": {
      "universeName": "",
      "ybSoftwareVersion": "",
      "numNodes": 3,
      "isMultiAZ": true,
      "instanceType": "c4.2xlarge",
      "formType": "create",
      "accessKeyCode": "yugabyte-default",
      "spotPrice": "0.00",
      "useSpotPrice": IN_DEVELOPMENT_MODE,
      "assignPublicIP":  true
    }
  };

  if (isNonEmptyObject(currentUniverse.data) && ownProps.type === "Edit") {
    const primaryCluster = getPrimaryCluster(currentUniverse.data.universeDetails.clusters);
    if (isDefinedNotNull(primaryCluster)) {
      const userIntent = primaryCluster.userIntent;
      data.primary = {};
      data.primary.universeName = currentUniverse.data.name;
      data.formType = "edit";
      data.primary.assignPublicIP = userIntent.assignPublicIP;
      data.primary.provider = userIntent.provider;
      data.primary.numNodes = userIntent.numNodes;
      data.primary.replicationFactor = userIntent.replicationFactor;
      data.primary.instanceType = userIntent.instanceType;
      data.primary.ybSoftwareVersion = userIntent.ybSoftwareVersion;
      data.primary.accessKeyCode = userIntent.accessKeyCode;
      data.primary.spotPrice = normalizeToPositiveFloat(userIntent.spotPrice.toString());
      data.primary.useSpotPrice = parseFloat(userIntent.spotPrice) > 0.0;
      data.primary.diskIops = userIntent.deviceInfo.diskIops;
      data.primary.numVolumes = userIntent.deviceInfo.numVolumes;
      data.primary.volumeSize = userIntent.deviceInfo.volumeSize;
      data.primary.ebsType = userIntent.deviceInfo.ebsType;

      data.primary.regionList = primaryCluster.regions.map((item) => {
        return {value: item.uuid, name: item.name, label: item.name};
      });
      data.primary.masterGFlags = Object.keys(userIntent.masterGFlags).map((key) => {
        return {name: key, value: userIntent.masterGFlags[key]};
      });
      data.primary.tserverGFlags = Object.keys(userIntent.tserverGFlags).map((key) => {
        return {name: key, value: userIntent.tserverGFlags[key]};
      });
    }
  }

  const selector = formValueSelector('UniverseForm');

  return {
    universe: state.universe,
    tasks: state.tasks,
    cloud: state.cloud,
    softwareVersions: state.customer.softwareVersions,
    accessKeys: state.cloud.accessKeys,
    initialValues: data,
    formValues: selector(state,
      'formType', 'primary.universeName', 'primary.provider', 'primary.providerType', 'primary.regionList',
      'primary.numNodes', 'primary.instanceType', 'primary.replicationFactor', 'primary.ybSoftwareVersion', 'primary.accessKeyCode',
      'primary.masterGFlags', 'primary.tserverGFlags', 'primary.diskIops', 'primary.numVolumes', 'primary.volumeSize', 'primary.ebsType',
      'primary.diskIops', 'primary.spotPrice', 'primary.assignPublicIP', 'primary.mountPoints',
      'async.universeName', 'async.provider', 'async.providerType', 'async.regionList', 'async.replicationFactor',
      'async.numNodes', 'async.instanceType', 'async.deviceInfo', 'async.spotPrice', 'async.ybSoftwareVersion', 'async.accessKeyCode',
      'async.diskIops',  'async.numVolumes',  'async.volumeSize',  'async.ebsType', 'async.assignPublicIP', 'async.mountPoints',
      'spotPrice', 'useSpotPrice', 'masterGFlags', 'tserverGFlags')
  };
}

const asyncValidate = (values, dispatch ) => {
  return new Promise((resolve, reject) => {
    if (values.primary && isNonEmptyString(values.primary.universeName)) {
      dispatch(checkIfUniverseExists(values.primary.universeName)).then((response) => {
        if (response.payload.status !== 200 && values.formType !== "edit") {
          reject({"primary": {"universeName": 'Universe name already exists'}});
        } else {
          resolve();
        }
      });
    } else {
      resolve();
    }
  });
};


const validate = (values, props) => {
  const cloud = props.cloud;
  let currentProvider = null;
  const errors = {primary: {}};
  if (!isNonEmptyObject(values.primary)) {
    return;
  }
  if (isNonEmptyObject(values.primary) && isNonEmptyString(values.primary.provider)) {
    currentProvider = cloud.providers.data.find((provider) => provider.uuid === values.primary.provider);
  }
  if (!isNonEmptyString(values.primary.universeName)) {
    errors.universeName = 'Universe Name is Required';
  }
  if (currentProvider && currentProvider.code === "gcp") {
    const specialCharsRegex = /^[a-z0-9-]*$/;
    if(!specialCharsRegex.test(values.primary.universeName)) {
      errors.primary.universeName = 'GCP Universe name cannot contain capital letters or special characters except dashes';
    }
  }
  if (!isDefinedNotNull(values.primary.provider)) {
    errors.primary.provider = 'Provider Value is Required';
  }
  if (!isDefinedNotNull(values.primary.regionList)) {
    errors.primary.regionList = 'Region Value is Required';
  }
  if (!isDefinedNotNull(values.primary.instanceType)) {
    errors.primary.instanceType = 'Instance Type is Required';
  }
  if (values.useSpotPrice && values.primary.spotPrice === '0.00') {
    errors.primary.spotPrice = 'Spot Price must be greater than $0.00';
  }
  return errors;
};

const universeForm = reduxForm({
  form: 'UniverseForm',
  validate,
  asyncValidate,
  fields: formFieldNames
});

export default connect(mapStateToProps, mapDispatchToProps)(universeForm(UniverseForm));
