// Copyright (c) YugaByte, Inc.

import { reduxForm, formValueSelector } from 'redux-form';
import { connect } from 'react-redux';
import {
  fetchCustomerTasks,
  fetchCustomerTasksSuccess,
  fetchCustomerTasksFailure
} from '../../../actions/tasks';
import UniverseForm from './UniverseForm';
import {
  getInstanceTypeList,
  getRegionList,
  getRegionListResponse,
  getInstanceTypeListLoading,
  getInstanceTypeListResponse,
  getNodeInstancesForProvider,
  getNodesInstancesForProviderResponse,
  fetchAuthConfigList,
  fetchAuthConfigListResponse
} from '../../../actions/cloud';
import { fetchRunTimeConfigs, fetchRunTimeConfigsResponse, getTlsCertificates, getTlsCertificatesResponse } from '../../../actions/customers';
import {
  rollingUpgrade,
  rollingUpgradeResponse,
  createUniverse,
  createUniverseResponse,
  editUniverse,
  editUniverseResponse,
  configureUniverseTemplate,
  configureUniverseTemplateResponse,
  configureUniverseTemplateSuccess,
  configureUniverseTemplateLoading,
  configureUniverseResources,
  configureUniverseResourcesResponse,
  checkIfUniverseExists,
  setPlacementStatus,
  resetUniverseConfiguration,
  fetchUniverseInfo,
  fetchUniverseInfoResponse,
  fetchUniverseMetadata,
  fetchUniverseTasks,
  fetchUniverseTasksResponse,
  addUniverseReadReplica,
  editUniverseReadReplica,
  addUniverseReadReplicaResponse,
  editUniverseReadReplicaResponse,
  closeUniverseDialog
} from '../../../actions/universe';

import { openDialog, closeDialog } from '../../../actions/modal';

import {
  isNonEmptyArray,
  isDefinedNotNull,
  isNonEmptyObject,
  isNonEmptyString,
  isEmptyObject,
  makeFirstLetterUpperCase
} from '../../../utils/ObjectUtils';
import { getClusterByType } from '../../../utils/UniverseUtils';
import { EXPOSING_SERVICE_STATE_TYPES } from './ClusterFields';
import { toast } from 'react-toastify';
import { createErrorMessage } from '../../../utils/ObjectUtils';

const mapDispatchToProps = (dispatch) => {
  return {
    submitConfigureUniverse: (values, universeUUID = null) => {
      dispatch(configureUniverseTemplateLoading());
      return dispatch(configureUniverseTemplate(values)).then((response) => {
        if (response.error) {
          toast.error(createErrorMessage(response.payload));
          if (universeUUID) {
            dispatch(fetchUniverseInfo(universeUUID)).then((response) => {
              dispatch(fetchUniverseInfoResponse(response.payload));
            });
          }
        }
        return dispatch(configureUniverseTemplateResponse(response.payload));
      });
    },

    fetchUniverseResources: (payload) => {
      dispatch(configureUniverseResources(payload)).then((resourceData) => {
        dispatch(configureUniverseResourcesResponse(resourceData.payload));
      });
    },

    submitCreateUniverse: (values) => {
      return dispatch(createUniverse(values)).then((response) => {
        dispatch(getTlsCertificates()).then((response) => {
          dispatch(getTlsCertificatesResponse(response.payload));
        });
        if (response.error) {
          toast.error(createErrorMessage(response.payload));
        }
        return dispatch(createUniverseResponse(response.payload));
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
      return dispatch(addUniverseReadReplica(values, universeUUID)).then((response) => {
        return dispatch(addUniverseReadReplicaResponse(response.payload));
      });
    },

    submitEditUniverseReadReplica: (values, universeUUID) => {
      return dispatch(editUniverseReadReplica(values, universeUUID)).then((response) => {
        return dispatch(editUniverseReadReplicaResponse(response.payload));
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
      return dispatch(editUniverse(values, universeUUID)).then((response) => {
        if (response.error) {
          const errorMessage = response.payload?.response?.data?.error || response.payload.message;
          toast.error(errorMessage);
        }
        return dispatch(editUniverseResponse(response.payload));
      });
    },

    submitUniverseNodeResize: (values, universeUUID) => {
      return dispatch(rollingUpgrade(values, universeUUID)).then((response) => {
        if (!response.error) {
          dispatch(closeUniverseDialog());
        }
        return dispatch(rollingUpgradeResponse(response.payload));
      });
    },

    getInstanceTypeListItems: (provider, zones) => {
      dispatch(getInstanceTypeListLoading());
      dispatch(getInstanceTypeList(provider, zones)).then((response) => {
        dispatch(getInstanceTypeListResponse(response.payload));
      });
    },

    getKMSConfigs: () => {
      dispatch(fetchAuthConfigList()).then((response) => {
        dispatch(fetchAuthConfigListResponse(response.payload));
      });
    },

    getRegionListItems: (provider) => {
      dispatch(getRegionList(provider)).then((response) => {
        dispatch(getRegionListResponse(response.payload));
      });
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
      dispatch(configureUniverseTemplateSuccess({ data: universeDetail }));
      dispatch(configureUniverseResources(universeDetail)).then((resourceData) => {
        dispatch(configureUniverseResourcesResponse(resourceData.payload));
      });
    },

    closeModal: () => {
      dispatch(closeDialog());
    },

    showDeleteReadReplicaModal: () => {
      dispatch(openDialog('deleteReadReplicaModal'));
    },

    showFullMoveModal: () => {
      dispatch(openDialog('fullMoveModal'));
    },

    showSmartResizeModal: () => {
      dispatch(openDialog('smartResizeModal'));
    },

    showUpgradeNodesModal: () => {
      dispatch(openDialog('resizeNodesModal'));
    },

    fetchNodeInstanceList: (providerUUID) => {
      dispatch(getNodeInstancesForProvider(providerUUID)).then((response) => {
        dispatch(getNodesInstancesForProviderResponse(response.payload));
      });
    },
    fetchRunTimeConfigs: () => {
      return dispatch(fetchRunTimeConfigs('00000000-0000-0000-0000-000000000000',true)).then((response) =>
        dispatch(fetchRunTimeConfigsResponse(response.payload))
      );
    },
  };
};

const formFieldNames = [
  'formType',
  'primary.universeName',
  'primary.provider',
  'primary.providerType',
  'primary.regionList',
  'primary.numNodes',
  'primary.instanceType',
  'primary.ybSoftwareVersion',
  'primary.accessKeyCode',
  'primary.gFlags',
  'primary.masterGFlags',
  'primary.tserverGFlags',
  'primary.instanceTags',
  'primary.diskIops',
  'primary.throughput',
  'primary.numVolumes',
  'primary.volumeSize',
  'primary.storageType',
  'primary.assignPublicIP',
  'primary.useTimeSync',
  'primary.enableYSQL',
  'primary.enableYSQLAuth',
  'primary.ysqlPassword',
  'primary.enableYCQL',
  'primary.enableYCQLAuth',
  'primary.ycqlPassword',
  'primary.enableIPV6',
  'primary.enableExposingService',
  'primary.enableYEDIS',
  'primary.enableNodeToNodeEncrypt',
  'primary.enableClientToNodeEncrypt',
  'primary.enableEncryptionAtRest',
  'primary.selectEncryptionAtRestConfig',
  'primary.tlsCertificateId',
  'primary.mountPoints',
  'primary.awsArnString',
  'primary.useSystemd',
  'primary.dedicatedNodes',
  'async.universeName',
  'async.provider',
  'async.providerType',
  'async.regionList',
  'async.numNodes',
  'async.instanceType',
  'async.ybSoftwareVersion',
  'async.accessKeyCode',
  'async.assignPublicIP',
  'async.useTimeSync',
  'async.enableYSQL',
  'async.enableYSQLAuth',
  'async.enableYCQL',
  'async.enableYCQLAuth',
  'async.enableIPV6',
  'async.enableExposingService',
  'async.enableYEDIS',
  'async.enableNodeToNodeEncrypt',
  'async.enableClientToNodeEncrypt',
  'async.diskIops',
  'async.throughput',
  'async.mountPoints',
  'async.useSystemd',
  'async.dedicatedNodes',
  'masterGFlags',
  'tserverGFlags',
  'instanceTags',
  'gFlags',
  'asyncClusters'
];

const portFields = [
  'masterHttpPort',
  'masterRpcPort',
  'tserverHttpPort',
  'tserverRpcPort',
  'redisHttpPort',
  'redisRpcPort',
  'yqlHttpPort',
  'yqlRpcPort',
  'ysqlHttpPort',
  'ysqlRpcPort',
  'nodeExporterPort'
];

function getFormData(currentUniverse, formType, clusterType) {
  const {
    universeDetails: { clusters, encryptionAtRestConfig, rootCA }
  } = currentUniverse.data;
  const cluster = getClusterByType(clusters, clusterType);
  const data = {};
  if (isDefinedNotNull(cluster)) {
    const userIntent = cluster.userIntent;
    data[clusterType] = {};
    data[clusterType].universeName = currentUniverse.data.name;
    data.formType = formType;
    data[clusterType].assignPublicIP = userIntent.assignPublicIP;
    data[clusterType].useTimeSync = userIntent.useTimeSync;
    data[clusterType].enableYSQL = userIntent.enableYSQL;
    data[clusterType].enableYSQLAuth = userIntent.enableYSQLAuth;
    data[clusterType].enableYCQL = userIntent.enableYCQL;
    data[clusterType].enableYCQLAuth = userIntent.enableYCQLAuth;
    data[clusterType].enableIPV6 = userIntent.enableIPV6;
    data[clusterType].enableExposingService = userIntent.enableExposingService;
    data[clusterType].enableYEDIS = userIntent.enableYEDIS;
    data[clusterType].enableNodeToNodeEncrypt = userIntent.enableNodeToNodeEncrypt;
    data[clusterType].enableClientToNodeEncrypt = userIntent.enableClientToNodeEncrypt;
    data[clusterType].provider = userIntent.provider;
    data[clusterType].numNodes = userIntent.numNodes;
    data[clusterType].replicationFactor = userIntent.replicationFactor;
    data[clusterType].instanceType = userIntent.instanceType;
    data[clusterType].ybSoftwareVersion = userIntent.ybSoftwareVersion;
    data[clusterType].ybcSoftwareVersion = userIntent.ybcSoftwareVersion;
    data[clusterType].useSystemd = userIntent.useSystemd;
    data[clusterType].accessKeyCode = userIntent.accessKeyCode;
    data[clusterType].diskIops = userIntent.deviceInfo.diskIops;
    data[clusterType].throughput = userIntent.deviceInfo.throughput;
    data[clusterType].numVolumes = userIntent.deviceInfo.numVolumes;
    data[clusterType].volumeSize = userIntent.deviceInfo.volumeSize;
    data[clusterType].storageType = userIntent.deviceInfo.storageType;
    data[clusterType].mountPoints = userIntent.deviceInfo.mountPoints;
    data[clusterType].storageClass = userIntent.deviceInfo.storageClass;
    data[clusterType].dedicatedNodes = userIntent.dedicatedNodes;

    data[clusterType].regionList = cluster.regions.map((item) => {
      return { value: item.uuid, name: item.name, label: item.name };
    });
    //construct gflag component DS
    data[clusterType].gFlags = [];
    if (isNonEmptyObject(userIntent.masterGFlags)) {
      Object.keys(userIntent.masterGFlags).forEach((key) => {
        const masterObj = {};
        if (userIntent?.tserverGFlags?.hasOwnProperty(key)) {
          masterObj['TSERVER'] = userIntent.tserverGFlags[key];
        }
        masterObj['Name'] = key;
        masterObj['MASTER'] = userIntent.masterGFlags[key];
        data[clusterType].gFlags.push(masterObj);
      });
    }
    if (isNonEmptyObject(userIntent.tserverGFlags)) {
      Object.keys(userIntent.tserverGFlags).forEach((key) => {
        const tserverObj = {};
        if (!userIntent.masterGFlags.hasOwnProperty(key)) {
          tserverObj['TSERVER'] = userIntent.tserverGFlags[key];
          tserverObj['Name'] = key;
          data[clusterType].gFlags.push(tserverObj);
        }
      });
    }
    data[clusterType].instanceTags = Object.keys(userIntent.instanceTags).map((key) => {
      return { name: key, value: userIntent.instanceTags[key] };
    });

    data[clusterType].tlsCertificateId = rootCA;
    if (encryptionAtRestConfig) {
      data[clusterType].enableEncryptionAtRest = encryptionAtRestConfig.encryptionAtRestEnabled;
      data[clusterType].selectEncryptionAtRestConfig = encryptionAtRestConfig.kmsConfigUUID;
    }
  }
  return data;
}

function mapStateToProps(state, ownProps) {
  const {
    universe: { currentUniverse },
    customer: {
      runtimeConfigs
    }
  } = state;
  let data = {
    formType: 'Create',
    primary: {
      universeName: '',
      ybSoftwareVersion: '',
      ybcSoftwareVersion: '',
      numNodes: 3,
      isMultiAZ: true,
      instanceType: 'c5.4xlarge',
      accessKeyCode: '',
      assignPublicIP: true,
      useSystemd: false,
      useTimeSync: true,
      enableYSQL: true,
      enableYSQLAuth: true,
      enableYCQL: true,
      enableYCQLAuth: true,
      enableIPV6: false,
      enableExposingService: EXPOSING_SERVICE_STATE_TYPES['Unexposed'],
      enableYEDIS: false,
      enableNodeToNodeEncrypt: true,
      enableClientToNodeEncrypt: true,
      enableEncryptionAtRest: false,
      awsArnString: '',
      selectEncryptionAtRestConfig: null,
      diskIops: null,
      throughput: null,
      dedicatedNodes: false,
    },
    async: {
      universeName: '',
      ybSoftwareVersion: '',
      ybcSoftwareVersion: '',
      numNodes: 3,
      isMultiAZ: true,
      assignPublicIP: true,
      useSystemd: false,
      useTimeSync: true,
      enableYSQL: true,
      enableYSQLAuth: true,
      enableYCQL: true,
      enableYCQLAuth: true,
      enableIPV6: false,
      enableExposingService: EXPOSING_SERVICE_STATE_TYPES['Unexposed'],
      enableYEDIS: false,
      enableNodeToNodeEncrypt: true,
      enableClientToNodeEncrypt: true,
      diskIops: null,
      throughput: null,
      dedicatedNodes: false,
    }
  };

  if (isNonEmptyObject(currentUniverse.data) && ownProps.type !== 'Create') {
    // TODO (vit.pankin): don't like this type having Async in it,
    // it should be clusterType or currentView
    const formResult = getFormData(
      currentUniverse,
      ownProps.type,
      ownProps.type === 'Async' ? 'async' : 'primary'
    );
    data = isEmptyObject(formResult) ? data : formResult;
  }

  const selector = formValueSelector('UniverseForm');

  return {
    universe: state.universe,
    modal: state.modal,
    tasks: state.tasks,
    cloud: state.cloud,
    runtimeConfigs,
    softwareVersions: state.customer.softwareVersions,
    userCertificates: state.customer.userCertificates,
    accessKeys: state.cloud.accessKeys,
    initialValues: data,
    featureFlags: state.featureFlags,
    formValues: selector(
      state,
      'formType',
      'primary.universeName',
      'primary.provider',
      'primary.providerType',
      'primary.regionList',
      'primary.numNodes',
      'primary.instanceType',
      'primary.replicationFactor',
      'primary.ybSoftwareVersion',
      'primary.accessKeyCode',
      'primary.gFlags',
      'primary.masterGFlags',
      'primary.tserverGFlags',
      'primary.instanceTags',
      'primary.numVolumes',
      'primary.volumeSize',
      'primary.storageType',
      'primary.diskIops',
      'primary.throughput',
      'primary.assignPublicIP',
      'primary.mountPoints',
      'primary.useTimeSync',
      'primary.enableYSQL',
      'primary.enableYSQLAuth',
      'primary.ysqlPassword',
      'primary.enableYCQL',
      'primary.enableYCQLAuth',
      'primary.ycqlPassword',
      'primary.enableIPV6',
      'primary.enableExposingService',
      'primary.enableYEDIS',
      'primary.enableNodeToNodeEncrypt',
      'primary.enableClientToNodeEncrypt',
      'primary.enableEncryptionAtRest',
      'primary.selectEncryptionAtRestConfig',
      'primary.tlsCertificateId',
      'primary.awsArnString',
      'primary.masterHttpPort',
      'primary.masterRpcPort',
      'primary.tserverHttpPort',
      'primary.tserverRpcPort',
      'primary.redisHttpPort',
      'primary.redisRpcPort',
      'primary.yqlHttpPort',
      'primary.yqlRpcPort',
      'primary.ysqlHttpPort',
      'primary.ysqlRpcPort',
      'primary.nodeExporterPort',
      'primary.useSystemd',
      'primary.ybcSoftwareVersion',
      'primary.dedicatedNodes',
      'async.universeName',
      'async.provider',
      'async.providerType',
      'async.regionList',
      'async.replicationFactor',
      'async.numNodes',
      'async.instanceType',
      'async.deviceInfo',
      'async.ybSoftwareVersion',
      'async.ybcSoftwareVersion',
      'async.accessKeyCode',
      'async.diskIops',
      'async.throughput',
      'async.numVolumes',
      'async.volumeSize',
      'async.storageType',
      'async.assignPublicIP',
      'async.enableYSQL',
      'async.enableYSQLAuth',
      'async.enableYCQL',
      'async.enableYCQLAuth',
      'async.enableIPV6',
      'async.enableExposingService',
      'async.enableYEDIS',
      'async.enableNodeToNodeEncrypt',
      'async.enableClientToNodeEncrypt',
      'async.mountPoints',
      'async.useTimeSync',
      'async.useSystemd',
      'async.dedicatedNodes',
      'masterGFlags',
      'gFlags',
      'tserverGFlags',
      'instanceTags'
    )
  };
}

const asyncValidate = (values, dispatch) => {
  return new Promise((resolve, reject) => {
    if (
      values.primary &&
      isNonEmptyString(values.primary.universeName) &&
      values.formType !== 'Async'
    ) {
      dispatch(checkIfUniverseExists(values.primary.universeName)).then((response) => {
        if (
          response.payload.status === 200 &&
          values.formType !== 'Edit' &&
          response.payload.data.length > 0
        ) {
          reject({ primary: { universeName: 'Universe name already exists' } });
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
  const currentClusterData = values[clusterType];
  if (isEmptyObject(currentClusterData)) {
    return errors;
  }
  const cloud = props.cloud;
  let currentProvider;
  if (isNonEmptyObject(currentClusterData) && isNonEmptyString(currentClusterData.provider)) {
    currentProvider = cloud.providers.data.find(
      (provider) => provider.uuid === currentClusterData.provider
    );
  }

  if (clusterType === 'primary') {
    const currentProviderCode = currentProvider?.code;
    if (!isNonEmptyString(currentClusterData.universeName)) {
      errors.universeName = 'Universe Name is Required';
    }
    if (currentProviderCode === 'gcp' || currentProviderCode === 'kubernetes') {
      const specialCharsRegex = /^[a-z0-9-]*$/;
      const errorProviderName = currentProviderCode === 'gcp' ? 
          currentProviderCode.toUpperCase() : makeFirstLetterUpperCase(currentProviderCode);

      if (!specialCharsRegex.test(currentClusterData.universeName)) {
        errors.universeName =
          `${errorProviderName} Universe name cannot contain capital letters or special characters except dashes`;
      }
    }
    if (
      currentClusterData.enableEncryptionAtRest &&
      !currentClusterData.selectEncryptionAtRestConfig
    ) {
      errors.selectEncryptionAtRestConfig = 'KMS Config is Required for Encryption at Rest';
    }

    const notUniquePortError = 'Port number should be unique';
    const portMap = new Map();
    portFields.forEach((portField) => {
      if (portMap.has(currentClusterData[portField])) {
        if (!errors.hasOwnProperty(portMap.get(currentClusterData[portField]))) {
          errors[portMap.get(currentClusterData[portField])] = notUniquePortError;
        }
        errors[portField] = notUniquePortError;
      } else {
        portMap.set(currentClusterData[portField], portField);
      }
    });
  }

  if (isEmptyObject(currentProvider)) {
    errors.provider = 'Provider Value is Required';
  }
  if (!isNonEmptyArray(currentClusterData.regionList)) {
    errors.regionList = 'Region Value is Required';
  }
  if (!isDefinedNotNull(currentClusterData.instanceType)) {
    errors.instanceType = 'Instance Type is Required';
  }
  return errors;
};

const validate = (values, props) => {
  const errors = {};
  const { type } = props;
  // TODO: once we have the currentView property properly set, we should use that
  // to do appropriate validation.
  if (type === 'Create' || type === 'Edit') {
    errors.primary = validateProviderFields(values, props, 'primary');
  } else if (type === 'Async') {
    errors.async = validateProviderFields(values, props, 'async');
  }

  return errors;
};

const universeForm = reduxForm({
  form: 'UniverseForm',
  validate,
  asyncValidate,
  fields: formFieldNames,
  asyncChangeFields: ['primary.universeName', 'async.universeName']
});

export default connect(mapStateToProps, mapDispatchToProps)(universeForm(UniverseForm));
