// Copyright (c) YugaByte, Inc.

import { reduxForm, formValueSelector } from 'redux-form';
import { connect } from 'react-redux';
import { fetchCustomerTasks, fetchCustomerTasksSuccess, fetchCustomerTasksFailure } from '../../../actions/tasks';
import UniverseForm from './UniverseForm';
import { getInstanceTypeList, getRegionList, getRegionListResponse, getInstanceTypeListResponse,
         getNodeInstancesForProvider, getNodesInstancesForProviderResponse, getSuggestedSpotPrice,
         getSuggestedSpotPriceResponse, resetSuggestedSpotPrice } from '../../../actions/cloud';
import { createUniverse, createUniverseResponse, editUniverse, editUniverseResponse,
         configureUniverseTemplate, configureUniverseTemplateResponse, configureUniverseTemplateSuccess,
         configureUniverseResources, configureUniverseResourcesResponse,
         checkIfUniverseExists, setPlacementStatus, resetUniverseConfiguration,
         fetchUniverseInfo, fetchUniverseInfoResponse, fetchUniverseMetadata, fetchUniverseTasks,
         fetchUniverseTasksResponse, addUniverseReadReplica, editUniverseReadReplica, 
         addUniverseReadReplicaResponse, editUniverseReadReplicaResponse, closeUniverseDialog } from '../../../actions/universe';

import { openDialog, closeDialog } from '../../../actions/modal';

import { isNonEmptyArray, isDefinedNotNull, isNonEmptyObject, isNonEmptyString, normalizeToPositiveFloat, isEmptyObject }
  from '../../../utils/ObjectUtils';
import { IN_DEVELOPMENT_MODE } from '../../../config';
import { getClusterByType } from '../../../utils/UniverseUtils';

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

    submitAddUniverseReadReplica: (values, universeUUID) => {
      dispatch(addUniverseReadReplica(values, universeUUID)).then((response) => {
        dispatch(addUniverseReadReplicaResponse(response.payload));
      });
    },

    submitEditUniverseReadReplica: (values, universeUUID) => {
      dispatch(editUniverseReadReplica(values, universeUUID)).then((response) => {
        dispatch(editUniverseReadReplicaResponse(response.payload));
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
      dispatch(closeUniverseDialog());
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

    closeModal: () => {
      dispatch(closeDialog());
    },

    showDeleteReadReplicaModal: () => {
      dispatch(openDialog("deleteReadReplicaModal"));
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
    'primary.volumeSize', 'primary.ebsType', 'primary.assignPublicIP', 'primary.useTimeSync', 'primary.mountPoints',
    'async.universeName', 'async.provider', 'async.providerType', 'async.spotPrice', 'async.regionList', 'async.numNodes',
    'async.instanceType', 'async.ybSoftwareVersion', 'async.accessKeyCode', 'async.assignPublicIP', 'async.useTimeSync', 'async.mountPoints',
    'spotPrice', 'useSpotPrice', 'masterGFlags', 'tserverGFlags', 'asyncClusters'];


function getFormData(currentUniverse, formType, clusterType) {
  const cluster = getClusterByType(currentUniverse.data.universeDetails.clusters, clusterType);
  const data = {};
  if (isDefinedNotNull(cluster)) {
    const userIntent = cluster.userIntent;
    data[clusterType] = {};
    data[clusterType].universeName = currentUniverse.data.name;
    data.formType = formType;
    data[clusterType].assignPublicIP = userIntent.assignPublicIP;
    data[clusterType].provider = userIntent.provider;
    data[clusterType].numNodes = userIntent.numNodes;
    data[clusterType].replicationFactor = userIntent.replicationFactor;
    data[clusterType].instanceType = userIntent.instanceType;
    data[clusterType].ybSoftwareVersion = userIntent.ybSoftwareVersion;
    data[clusterType].accessKeyCode = userIntent.accessKeyCode;
    data[clusterType].spotPrice = normalizeToPositiveFloat(userIntent.spotPrice.toString());
    data[clusterType].useSpotPrice = parseFloat(userIntent.spotPrice) > 0.0;
    data[clusterType].diskIops = userIntent.deviceInfo.diskIops;
    data[clusterType].numVolumes = userIntent.deviceInfo.numVolumes;
    data[clusterType].volumeSize = userIntent.deviceInfo.volumeSize;
    data[clusterType].ebsType = userIntent.deviceInfo.ebsType;
    data[clusterType].mountPoints = userIntent.deviceInfo.mountPoints;
    data[clusterType].storageClass = userIntent.deviceInfo.storageClass;

    data[clusterType].regionList = cluster.regions.map((item) => {
      return {value: item.uuid, name: item.name, label: item.name};
    });
    data[clusterType].masterGFlags = Object.keys(userIntent.masterGFlags).map((key) => {
      return {name: key, value: userIntent.masterGFlags[key]};
    });
    data[clusterType].tserverGFlags = Object.keys(userIntent.tserverGFlags).map((key) => {
      return {name: key, value: userIntent.tserverGFlags[key]};
    });
  }
  return data;
}

function mapStateToProps(state, ownProps) {
  const {universe: { currentUniverse }} = state;
  let data = {
    "formType": "Create",
    "primary": {
      "universeName": "",
      "ybSoftwareVersion": "",
      "numNodes": 3,
      "isMultiAZ": true,
      "instanceType": "c4.2xlarge",
      "accessKeyCode": "yugabyte-default",
      "spotPrice": "0.00",
      "useSpotPrice": IN_DEVELOPMENT_MODE,
      "assignPublicIP":  true,
      "useTimeSync": false
    },
    "async": {
      "universeName": "",
      "numNodes": 3,
      "isMultiAZ": true,
      "spotPrice": "0.00",
      "useSpotPrice": IN_DEVELOPMENT_MODE,
      "assignPublicIP":  true,
      "useTimeSync": false
    }
  };

  if (isNonEmptyObject(currentUniverse.data) && ownProps.type !== "Create") {
    // TODO (vit.pankin): don't like this type having Async in it,
    // it should be clusterType or currentView
    data = getFormData(currentUniverse, ownProps.type,
      ownProps.type === "Async" ? "async": "primary");
  }

  const selector = formValueSelector('UniverseForm');

  return {
    universe: state.universe,
    modal: state.modal,
    tasks: state.tasks,
    cloud: state.cloud,
    softwareVersions: state.customer.softwareVersions,
    accessKeys: state.cloud.accessKeys,
    initialValues: data,
    formValues: selector(state,
      'formType', 'primary.universeName', 'primary.provider', 'primary.providerType', 'primary.regionList',
      'primary.numNodes', 'primary.instanceType', 'primary.replicationFactor', 'primary.ybSoftwareVersion', 'primary.accessKeyCode',
      'primary.masterGFlags', 'primary.tserverGFlags', 'primary.diskIops', 'primary.numVolumes', 'primary.volumeSize', 'primary.ebsType',
      'primary.diskIops', 'primary.spotPrice', 'primary.assignPublicIP', 'primary.mountPoints', 'primary.useTimeSync', 'primary.storageClass',
      'async.universeName', 'async.provider', 'async.providerType', 'async.regionList', 'async.replicationFactor',
      'async.numNodes', 'async.instanceType', 'async.deviceInfo', 'async.spotPrice', 'async.ybSoftwareVersion', 'async.accessKeyCode',
      'async.diskIops',  'async.numVolumes',  'async.volumeSize',  'async.ebsType', 'async.assignPublicIP', 'async.mountPoints',
      'async.useTimeSync', 'async.storageClass', 'spotPrice', 'useSpotPrice', 'masterGFlags', 'tserverGFlags')
  };
}

const asyncValidate = (values, dispatch ) => {
  return new Promise((resolve, reject) => {
    if (values.primary && isNonEmptyString(values.primary.universeName) && values.formType !== "Async" ) {
      dispatch(checkIfUniverseExists(values.primary.universeName)).then((response) => {
        if (response.payload.status !== 200 && values.formType !== "Edit") {
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

const validateProviderFields = (values, props, clusterType) => {
  const errors = {};
  if (isEmptyObject(values[clusterType])) {
    return errors;
  }
  const cloud = props.cloud;
  let currentProvider;
  if (isNonEmptyObject(values[clusterType]) && isNonEmptyString(values[clusterType].provider)) {
    currentProvider = cloud.providers.data.find((provider) => provider.uuid === values[clusterType].provider);
  }

  if (clusterType === "primary") {
    if (!isNonEmptyString(values[clusterType].universeName)) {
      errors.universeName = 'Universe Name is Required';
    }
    if (currentProvider && currentProvider.code === "gcp") {
      const specialCharsRegex = /^[a-z0-9-]*$/;
      if(!specialCharsRegex.test(values[clusterType].universeName)) {
        errors.universeName = 'GCP Universe name cannot contain capital letters or special characters except dashes';
      }
    }
  }

  if (isEmptyObject(currentProvider)) {
    errors.provider = 'Provider Value is Required';
  }
  if (!isNonEmptyArray(values[clusterType].regionList)) {
    errors.regionList = 'Region Value is Required';
  }
  if (!isDefinedNotNull(values[clusterType].instanceType)) {
    errors.instanceType = 'Instance Type is Required';
  }
  if (values.useSpotPrice && values[clusterType].spotPrice === '0.00') {
    errors.spotPrice = 'Spot Price must be greater than $0.00';
  }
  return errors;
};

const validate = (values, props) => {
  const errors = {};
  const { type } = props;
  // TODO: once we have the currentView property properly set, we should use that
  // to do appropriate validation.
  if (type === "Create" || type === "Edit") {
    errors.primary = validateProviderFields(values, props, "primary");
  } else if (type === "Async") {
    errors.async = validateProviderFields(values, props, "async");
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
