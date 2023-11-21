import _ from 'lodash';
import { browserHistory } from 'react-router';
import { toast } from 'react-toastify';
import {
  CloudType,
  ClusterType,
  Cluster,
  UniverseDetails,
  UniverseConfigure,
  UniverseFormData,
  UserIntent,
  Gflag,
  DEFAULT_FORM_DATA,
  InstanceTag,
  InstanceTags,
  MasterPlacementMode,
  NodeDetails
} from './dto';
import { UniverseFormContextState } from '../UniverseFormContainer';
import {
  ASYNC_FIELDS,
  PRIMARY_FIELDS,
  INHERITED_FIELDS_FROM_PRIMARY,
  TOAST_AUTO_DISMISS_INTERVAL
} from './constants';
import { api } from './api';
import { getPlacementsFromCluster } from '../form/fields/PlacementsField/PlacementsFieldHelper';

export const transitToUniverse = (universeUUID?: string) =>
  universeUUID
    ? browserHistory.push(`/universes/${universeUUID}/tasks`)
    : browserHistory.push(`/universes`);

export const getClusterByType = (universeData: UniverseDetails, clusterType: ClusterType) =>
  universeData?.clusters?.find((cluster) => cluster.clusterType === clusterType);

export const getPrimaryCluster = (universeData: UniverseDetails) =>
  getClusterByType(universeData, ClusterType.PRIMARY);

export const getAsyncCluster = (universeData: UniverseDetails) =>
  getClusterByType(universeData, ClusterType.ASYNC);

export const getCurrentVersion = (universeData: UniverseDetails) => {
  let currentVersion = null;
  const primaryCluster = getPrimaryCluster(universeData);
  currentVersion = primaryCluster?.userIntent?.ybSoftwareVersion;
  return currentVersion;
};

export const getUniverseName = (universeData: UniverseDetails) =>
  _.get(getClusterByType(universeData, ClusterType.PRIMARY), 'userIntent.universeName');

export const getPrimaryFormData = (universeData: UniverseDetails) =>
  getFormData(universeData, ClusterType.PRIMARY);

export const getAsyncFormData = (universeData: UniverseDetails) =>
  getFormData(universeData, ClusterType.ASYNC);

//returns fields needs to be copied from Primary to Async in Create+RR flow
export const getPrimaryInheritedValues = (formData: UniverseFormData) =>
  _.pick(formData, INHERITED_FIELDS_FROM_PRIMARY);

//create error msg from reponse payload
export const createErrorMessage = (payload: any) => {
  const structuredError = payload?.response?.data?.error;
  if (structuredError) {
    if (typeof structuredError == 'string') {
      return structuredError;
    }
    const message = Object.keys(structuredError)
      .map((fieldName) => {
        const messages = structuredError[fieldName];
        return fieldName + ': ' + messages.join(', ');
      })
      .join('\n');
    return message;
  }
  return payload.message;
};

//Filter form data by cluster type
export const filterFormDataByClusterType = (
  formData: UniverseFormData,
  clusterType: ClusterType
) => {
  const formFields = clusterType === ClusterType.PRIMARY ? PRIMARY_FIELDS : ASYNC_FIELDS;
  const defaultFormData = _.pick(DEFAULT_FORM_DATA, formFields);
  const currentFormData = _.pick(formData, formFields);
  return _.merge(defaultFormData, currentFormData) as UniverseFormData;
  // return (_.pick(formData, formFields) as unknown) as UniverseFormData;
};

//transform gflags - to consume it in the form
export const transformGFlagToFlagsArray = (
  masterGFlags: Record<string, any> = {},
  tserverGFlags: Record<string, any> = {}
) => {
  // convert { flagname:value } to => { Name:flagname, TSERVER: value , MASTER: value }
  const tranformFlagsByFlagType = (gFlags: Record<string, any>, flagType: string) => [
    ...Object.keys(gFlags).map((key: string) => ({
      Name: key,
      [flagType]: gFlags[key]
    }))
  ];

  //merge tserver and master glags value into single object if flag Name is same
  return _.values(
    _.merge(
      _.keyBy(tranformFlagsByFlagType(masterGFlags, 'MASTER'), 'Name'),
      _.keyBy(tranformFlagsByFlagType(tserverGFlags, 'TSERVER'), 'Name')
    )
  );
};

export const transformSpecificGFlagToFlagsArray = (specificGFlags: Record<string, any> = {}) => {
  const masterFlags = specificGFlags?.perProcessFlags?.value?.MASTER;
  const tserverFlags = specificGFlags?.perProcessFlags?.value?.TSERVER;
  return transformGFlagToFlagsArray(masterFlags, tserverFlags);
};

//transform gflags - for universe configure route
export const transformFlagArrayToObject = (
  flagsArray: Gflag[] = []
): { masterGFlags: Record<string, any>; tserverGFlags: Record<string, any> } => {
  let masterGFlags = {},
    tserverGFlags = {};
  flagsArray.forEach((flag: Gflag) => {
    if (flag?.hasOwnProperty('MASTER')) masterGFlags[flag.Name] = `${flag['MASTER']}`;
    if (flag?.hasOwnProperty('TSERVER')) tserverGFlags[flag.Name] = `${flag['TSERVER']}`;
  });
  return { masterGFlags, tserverGFlags };
};

//transform instance tags - to consume it in the form
export const transformInstanceTags = (instanceTags: Record<string, string> = {}) =>
  Object.entries(instanceTags).map(([key, val]) => ({
    name: key,
    value: val
  }));

//transform instance tags - for universe configure route
export const transformTagsArrayToObject = (instanceTags: InstanceTags) =>
  instanceTags.reduce((tagsObj: Record<string, string>, tag: InstanceTag) => {
    if (tag?.name && tag?.value) tagsObj[tag.name] = `${tag.value}`;
    return tagsObj;
  }, {});

//Transform universe data to form data
export const getFormData = (universeData: UniverseDetails, clusterType: ClusterType) => {
  const { communicationPorts, encryptionAtRestConfig, rootCA } = universeData;
  const cluster = getClusterByType(universeData, clusterType);

  if (!cluster) return DEFAULT_FORM_DATA;

  const { userIntent } = cluster;

  let data: UniverseFormData = {
    cloudConfig: {
      universeName: userIntent.universeName,
      provider: {
        code: userIntent.providerType,
        uuid: userIntent.provider
      },
      regionList: userIntent.regionList,
      numNodes: userIntent.numNodes,
      replicationFactor: userIntent.replicationFactor,
      placements: getPlacementsFromCluster(cluster),
      defaultRegion: cluster?.placementInfo?.cloudList[0]?.defaultRegion ?? null,
      mastersInDefaultRegion: universeData.mastersInDefaultRegion,
      masterPlacement: userIntent.dedicatedNodes
        ? MasterPlacementMode.DEDICATED
        : MasterPlacementMode.COLOCATED,
      autoPlacement: true //** */,
    },
    instanceConfig: {
      instanceType: userIntent.instanceType,
      deviceInfo: userIntent.deviceInfo,
      useSpotInstance: userIntent.useSpotInstance,
      assignPublicIP: !!userIntent.assignPublicIP,
      useTimeSync: !!userIntent.useTimeSync,
      enableClientToNodeEncrypt: !!userIntent.enableClientToNodeEncrypt,
      enableNodeToNodeEncrypt: !!userIntent.enableNodeToNodeEncrypt,
      enableYSQL: userIntent.enableYSQL,
      enableYSQLAuth: userIntent.enableYSQLAuth,
      enableYCQL: userIntent.enableYCQL,
      enableYCQLAuth: userIntent.enableYCQLAuth,
      enableYEDIS: !!userIntent.enableYEDIS,
      enableEncryptionAtRest: !!encryptionAtRestConfig.encryptionAtRestEnabled,
      kmsConfig: encryptionAtRestConfig?.kmsConfigUUID ?? null,
      tserverK8SNodeResourceSpec: userIntent.tserverK8SNodeResourceSpec,
      rootCA
    },
    advancedConfig: {
      useSystemd: userIntent.useSystemd,
      awsArnString: userIntent.awsArnString ?? null,
      enableIPV6: !!userIntent.enableIPV6,
      enableExposingService: userIntent.enableExposingService,
      accessKeyCode: userIntent.accessKeyCode ?? null,
      ybSoftwareVersion: userIntent.ybSoftwareVersion,
      communicationPorts,
      customizePort: false, //** */
      ybcPackagePath: null //** */
    },
    instanceTags: transformInstanceTags(userIntent.instanceTags),
    gFlags: userIntent?.specificGFlags
      ? transformSpecificGFlagToFlagsArray(userIntent?.specificGFlags)
      : transformGFlagToFlagsArray(userIntent.masterGFlags, userIntent.tserverGFlags),
    azOverrides: userIntent.azOverrides,
    universeOverrides: userIntent.universeOverrides,
    inheritFlagsFromPrimary: userIntent?.specificGFlags?.inheritFromPrimary
  };

  if (data.cloudConfig.masterPlacement === MasterPlacementMode.DEDICATED) {
    data.instanceConfig.masterInstanceType = userIntent.masterInstanceType;
    data.instanceConfig.masterDeviceInfo = userIntent.masterDeviceInfo;
  }

  if (
    data.cloudConfig.provider?.code === CloudType.kubernetes &&
    data.cloudConfig.masterPlacement === MasterPlacementMode.DEDICATED
  ) {
    data.instanceConfig.masterK8SNodeResourceSpec = userIntent.masterK8SNodeResourceSpec;
  }

  return data;
};

//Transform form data to intent

export const getUserIntent = (
  { formData }: { formData: UniverseFormData },
  clusterType: ClusterType = ClusterType.PRIMARY,
  featureFlags: Record<string, any>
) => {
  const enableRRGflags = featureFlags.test.enableRRGflags || featureFlags.released.enableRRGflags;
  const {
    cloudConfig,
    instanceConfig,
    advancedConfig,
    instanceTags,
    gFlags,
    azOverrides,
    universeOverrides,
    inheritFlagsFromPrimary
  } = formData;
  const { masterGFlags, tserverGFlags } = transformFlagArrayToObject(gFlags);

  let intent: UserIntent = {
    universeName: cloudConfig.universeName,
    provider: cloudConfig.provider?.uuid as string,
    providerType: cloudConfig.provider?.code as CloudType,
    regionList: cloudConfig.regionList,
    numNodes: Number(cloudConfig.numNodes),
    replicationFactor: cloudConfig.replicationFactor,
    dedicatedNodes: cloudConfig.masterPlacement === MasterPlacementMode.DEDICATED,
    instanceType: instanceConfig.instanceType,
    deviceInfo: instanceConfig.deviceInfo,
    assignPublicIP: instanceConfig.assignPublicIP,
    enableNodeToNodeEncrypt: instanceConfig.enableNodeToNodeEncrypt,
    enableClientToNodeEncrypt: instanceConfig.enableClientToNodeEncrypt,
    enableYSQL: instanceConfig.enableYSQL,
    enableYSQLAuth: instanceConfig.enableYSQLAuth,
    enableYCQL: instanceConfig.enableYCQL,
    enableYCQLAuth: instanceConfig.enableYCQLAuth,
    useTimeSync: instanceConfig.useTimeSync,
    enableYEDIS: instanceConfig.enableYEDIS,
    useSpotInstance: instanceConfig.useSpotInstance,
    tserverK8SNodeResourceSpec: instanceConfig.tserverK8SNodeResourceSpec,
    accessKeyCode: advancedConfig.accessKeyCode,
    ybSoftwareVersion: advancedConfig.ybSoftwareVersion,
    enableIPV6: advancedConfig.enableIPV6,
    enableExposingService: advancedConfig.enableExposingService,
    useSystemd: advancedConfig.useSystemd
  };

  if (enableRRGflags) {
    if (clusterType === ClusterType.ASYNC && inheritFlagsFromPrimary) {
      intent.specificGFlags = {
        inheritFromPrimary: true,
        perProcessFlags: {},
        perAZ: {}
      };
    } else {
      intent.specificGFlags = {
        inheritFromPrimary: false,
        perProcessFlags: {
          value: {
            MASTER: masterGFlags,
            TSERVER: tserverGFlags
          }
        },
        perAZ: {}
      };
    }
  } else {
    if (!_.isEmpty(masterGFlags)) intent.masterGFlags = masterGFlags;
    if (!_.isEmpty(tserverGFlags)) intent.tserverGFlags = tserverGFlags;
  }

  if (!_.isEmpty(advancedConfig.awsArnString)) intent.awsArnString = advancedConfig.awsArnString;
  if (!_.isEmpty(instanceTags)) intent.instanceTags = transformTagsArrayToObject(instanceTags);
  if (!_.isEmpty(azOverrides)) intent.azOverrides = azOverrides;
  if (!_.isEmpty(universeOverrides)) intent.universeOverrides = universeOverrides;

  if (
    cloudConfig.provider?.code === CloudType.kubernetes &&
    cloudConfig.masterPlacement === MasterPlacementMode.DEDICATED
  ) {
    intent.masterK8SNodeResourceSpec = instanceConfig.masterK8SNodeResourceSpec;
  }

  if (cloudConfig.masterPlacement === MasterPlacementMode.DEDICATED) {
    intent.masterInstanceType = instanceConfig.masterInstanceType;
    intent.masterDeviceInfo = instanceConfig.masterDeviceInfo;
  }

  if (instanceConfig.enableYSQLAuth && instanceConfig.ysqlPassword)
    intent.ysqlPassword = instanceConfig.ysqlPassword;

  if (instanceConfig.enableYCQLAuth && instanceConfig.ycqlPassword)
    intent.ycqlPassword = instanceConfig.ycqlPassword;

  return intent;
};

//Form Submit helpers
export const createUniverse = async ({
  configurePayload,
  universeContextData
}: {
  configurePayload: UniverseConfigure;
  universeContextData: UniverseFormContextState;
}) => {
  try {
    // in create mode no configure call is made with all form fields ( intent )
    const configPayload = configurePayload as UniverseDetails;
    const finalPayload = await api.universeConfigure(
      _.merge(universeContextData.universeConfigureTemplate, configPayload)
    );

    //patch - start -- some data format changes after configure call
    const clusterIndex = finalPayload.clusters.findIndex(
      (cluster: Cluster) => cluster.clusterType === ClusterType.PRIMARY
    );
    const userIntent = finalPayload.clusters[clusterIndex].userIntent;
    finalPayload.encryptionAtRestConfig = configPayload.encryptionAtRestConfig;
    if (userIntent.enableYCQLAuth)
      userIntent.ycqlPassword = configPayload.clusters[clusterIndex].userIntent.ycqlPassword;
    if (userIntent.enableYSQLAuth)
      userIntent.ysqlPassword = configPayload.clusters[clusterIndex].userIntent.ysqlPassword;

    if (finalPayload.nodeDetailsSet) {
      finalPayload.nodeDetailsSet = finalPayload.nodeDetailsSet.map((nodeDetail: NodeDetails) => {
        return {
          ...nodeDetail,
          cloudInfo: {
            ...nodeDetail.cloudInfo,
            assignPublicIP: !!userIntent.assignPublicIP
          }
        };
      });
    }
    //patch - end

    // now everything is ready to create universe
    let response = await api.createUniverse(finalPayload);

    //redirect to task page
    response?.universeUUID && transitToUniverse(response.universeUUID);
    return response;
  } catch (error) {
    console.error(error);
    toast.error(createErrorMessage(error), { autoClose: TOAST_AUTO_DISMISS_INTERVAL });
    return error;
  }
};

export const createReadReplica = async (configurePayload: UniverseConfigure) => {
  let universeUUID = configurePayload.universeUUID;
  if (!universeUUID) return false;
  try {
    // now everything is ready to create async cluster
    let response = await api.createCluster(configurePayload, universeUUID);
    response && transitToUniverse(universeUUID);
    return response;
  } catch (error) {
    console.error(error);
    toast.error(createErrorMessage(error), { autoClose: TOAST_AUTO_DISMISS_INTERVAL });
    return error;
  }
};

export const editReadReplica = async (configurePayload: UniverseConfigure) => {
  let universeUUID = configurePayload.universeUUID;
  if (!universeUUID) return false;
  try {
    // now everything is ready to edit universe
    let response = await api.editUniverse(configurePayload, universeUUID);
    response && transitToUniverse(universeUUID);
    return response;
  } catch (error) {
    console.error(error);
    toast.error(createErrorMessage(error), { autoClose: TOAST_AUTO_DISMISS_INTERVAL });
    return error;
  }
};
