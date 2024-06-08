import axios, { Canceler } from 'axios';
import {
  YBProviderMutation,
  YBProvider,
  InstanceTypeMutation,
  RegionMetadataResponse
} from '../../components/configRedesign/providerRedesign/types';
import {
  HostInfo,
  MetricsQueryParams,
  MetricsQueryResponse,
  Provider as Provider_Deprecated,
  SuggestedKubernetesConfig,
  UniverseNamespace,
  YBPSuccess
} from './dtos';
import { ROOT_URL } from '../../config';
import {
  AvailabilityZone,
  Region,
  Universe,
  UniverseDetails,
  InstanceType,
  AccessKey,
  Certificate,
  KmsConfig,
  UniverseConfigure,
  YBPTask
} from './dtos';
import { HaConfig, HaReplicationSchedule, HaPlatformInstance } from '../../components/ha/dtos';
import { DEFAULT_RUNTIME_GLOBAL_SCOPE } from '../../actions/customers';
import { UniverseTableFilters } from '../../actions/xClusterReplication';
import {
  KubernetesProvider,
  ProviderCode
} from '../../components/configRedesign/providerRedesign/constants';
import {
  DrConfig,
  DrConfigSafetimeResponse
} from '../../components/xcluster/disasterRecovery/dtos';

/**
 * @deprecated Use query key factories for more flexable key organization
 */
export enum QUERY_KEY {
  fetchUniverse = 'fetchUniverse',
  getProvidersList = 'getProvidersList',
  getRegionsList = 'getRegionsList',
  universeConfigure = 'universeConfigure',
  getInstanceTypes = 'getInstanceTypes',
  getDBVersions = 'getDBVersions',
  getDBVersionsByProvider = 'getDBVersionsByProvider',
  getAccessKeys = 'getAccessKeys',
  getCertificates = 'getCertificates',
  getKMSConfigs = 'getKMSConfigs',
  deleteCertificate = 'deleteCertificate',
  getHAConfig = 'getHAConfig',
  getHAReplicationSchedule = 'getHAReplicationSchedule',
  getHABackups = 'getHABackups',
  validateGflags = 'validateGflags',
  getMostUsedGflags = 'getMostUsedGflags',
  getAllGflags = 'getAllGflags',
  getGflagByName = 'getGlagByName'
}

// --------------------------------------------------------------------------------------
// React Query Key Factories
// --------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------
// TODO: Upgrade React Query to 3.17+ to get the change for supporting
//       annotating these as readonly query keys. (PLAT-4896)

export const taskQueryKey = {
  ALL: ['task'],
  customer: (customerUuid: string) => [...taskQueryKey.ALL, 'customer', customerUuid],
  universe: (universeUuid: string) => [...taskQueryKey.ALL, 'universe', universeUuid],
  provider: (providerUuid: string) => [...taskQueryKey.ALL, 'provider', providerUuid],
  xCluster: (xClusterUuid: string) => [...taskQueryKey.ALL, 'xCluster', xClusterUuid]
};

export const providerQueryKey = {
  ALL: ['provider'],
  detail: (providerUuid: string) => [...providerQueryKey.ALL, providerUuid]
};

export const hostInfoQueryKey = {
  ALL: ['hostInfo']
};

export const regionMetadataQueryKey = {
  ALL: ['regionMetadata'],
  detail: (providerCode: ProviderCode, kubernetesProvider?: KubernetesProvider) => [
    ...regionMetadataQueryKey.ALL,
    `${providerCode}${kubernetesProvider ? `_${kubernetesProvider}` : ''}`
  ]
};

export const universeQueryKey = {
  ALL: ['universe'],
  detail: (universeUuid: string | undefined) => [...universeQueryKey.ALL, universeUuid],
  tables: (universeUuid: string | undefined, filters: UniverseTableFilters) => [
    ...universeQueryKey.detail(universeUuid),
    'tables',
    { filters }
  ],
  namespaces: (universeUuid: string | undefined) => [
    ...universeQueryKey.detail(universeUuid),
    ,
    'namespaces'
  ]
};

export const runtimeConfigQueryKey = {
  ALL: ['runtimeConfig'],
  globalScope: () => [...runtimeConfigQueryKey.ALL, 'global'],
  customerScope: (customerUuid: string) => [...runtimeConfigQueryKey.ALL, 'customer', customerUuid],
  universeScope: (universeUuid: string) => [...runtimeConfigQueryKey.ALL, 'universe', universeUuid],
  providerScope: (providerUuid: string) => [...runtimeConfigQueryKey.ALL, 'provider', providerUuid]
};

export const instanceTypeQueryKey = {
  ALL: ['instanceType'],
  provider: (providerUuid: string) => [...instanceTypeQueryKey.ALL, 'provider', providerUuid]
};

export const suggestedKubernetesConfigQueryKey = {
  ALL: ['suggestedKubernetesConfig']
};

export const xClusterQueryKey = {
  ALL: ['xCluster'],
  detail: (xClusterConfigUuid: string) => [...xClusterQueryKey.ALL, xClusterConfigUuid]
};

export const drConfigQueryKey = {
  ALL: ['drConfig'],
  detail: (drConfigUuid: string | undefined) => [...drConfigQueryKey.ALL, drConfigUuid],
  safetimes: (drConfigUuid: string) => [...drConfigQueryKey.detail(drConfigUuid), 'safetimes']
};

export const metricQueryKey = {
  ALL: ['metric'],
  detail: (metricRequestParams: { [property: string]: any }) => [
    ...metricQueryKey.ALL,
    metricRequestParams
  ],
  /**
   * Usage:
   * Calling live() with no parameters returns a query key which can be used for invalidating all
   * live queries.
   * Invalidated queries will be refetched if the query still has observers.
   */
  live: (metricRequestParams?: { [property: string]: any }, range?: string, unit?: string) => {
    const { start, end, ...remainingRequestParams } = metricRequestParams ?? {};
    // For metric queries where we are interested in the last x units of data, we should
    // use the range and unit as part of the key instead of the concrete start and end time.
    // This helps us refetch in the background while serving the most recent data from
    // the cache. This is a better experience than hitting a loading spinner constantly on a
    // metric graph which updates every x seconds.
    return [
      ...metricQueryKey.ALL,
      'live',
      ...(metricRequestParams ? [remainingRequestParams] : []),
      ...(range || unit ? [{ range, unit }] : [])
    ];
  }
};

export const alertConfigQueryKey = {
  ALL: ['alertConfig'],
  list: (filters: unknown) => [...alertConfigQueryKey.ALL, { filters }]
};

export const alertTemplateQueryKey = {
  ALL: ['alertTempalte'],
  list: (filters: unknown) => [...alertTemplateQueryKey.ALL, { filters }]
};

// --------------------------------------------------------------------------------------
// API Constants
// --------------------------------------------------------------------------------------

export const ApiTimeout = {
  FETCH_TABLE_INFO: 90_000,
  FETCH_XCLUSTER_CONFIG: 120_000
} as const;

// --------------------------------------------------------------------------------------
// API Request Types
// --------------------------------------------------------------------------------------

export interface CreateDrConfigRequest {
  name: string;
  sourceUniverseUUID: string;
  targetUniverseUUID: string;
  dbs: string[]; // Database uuids (from source universe) selected for replication.
  bootstrapParams: {
    backupRequestParams?: {
      storageConfigUUID: string;
    };
  };

  dryRun?: boolean; // Run the pre-checks without actually running the subtasks
}

export interface EditDrConfigRequest {
  bootstrapParams: {
    backupRequestParams: {
      storageConfigUUID: string;
    };
  };
}

export interface ReplaceDrReplicaRequest {
  primaryUniverseUuid: string; // The current primary universe.
  drReplicaUniverseUuid: string; // The newly requested DR replica universe.
}

export interface DrSwitchoverRequest {
  // primaryUniverseUuid is new primary universe AFTER switchover (i.e. current DR replica universe).
  primaryUniverseUuid: string;
  // drReplicaUniverseUuid is the new DR replica universe AFTER switchover (i.e. current primary universe).
  drReplicaUniverseUuid: string;
}

export interface DrFailoverRequest {
  // primaryUniverseUuid is new primary universe AFTER failover (i.e. current DR replica universe).
  primaryUniverseUuid: string;
  // drReplicaUniverseUuid is the new DR replica universe AFTER failover (i.e. current primary universe).
  drReplicaUniverseUuid: string;
  namespaceIdSafetimeEpochUsMap: { [namespaceId: string]: string };
}

export interface RestartDrConfigRequest {
  dbs: string[]; // Database uuids (from the source universe) to be restarted.
}

export interface UpdateTablesInDrRequest {
  tables: string[];

  autoIncludeIndexTables?: boolean;
}

export interface CreateHaConfigRequest {
  cluster_key: string;

  // `accept_any_certificate` - When true will turn off certification validation while setting up the
  // HA config.
  accept_any_certificate?: boolean;
}

/**
 * Backend uses the same HAConfigFormData.class
 * */
export type EditHaConfigRequest = CreateHaConfigRequest;

export interface PromoteHaInstanceRequest {
  backup_file: string;
}

class ApiService {
  private cancellers: Record<string, Canceler> = {};

  private getCustomerId(): string {
    const customerId = localStorage.getItem('customerId');
    return customerId || '';
  }

  fetchHostInfo = () => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/host_info`;
    return axios.get<HostInfo>(requestUrl).then((response) => response.data);
  };

  fetchRuntimeConfigs = (
    configScope: string = DEFAULT_RUNTIME_GLOBAL_SCOPE,
    includeInherited = false
  ) => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/runtime_config/${configScope}?includeInherited=${includeInherited}`;
    return axios.get(requestUrl).then((response) => response.data);
  };

  findUniverseByName = (universeName: string): Promise<string[]> => {
    // auto-cancel previous request, if any
    if (this.cancellers.findUniverseByName) this.cancellers.findUniverseByName();

    // update cancellation stuff
    const source = axios.CancelToken.source();
    this.cancellers.findUniverseByName = source.cancel;

    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/universes/find?name=${universeName}`;
    return axios
      .get<string[]>(requestUrl, { cancelToken: source.token })
      .then((resp) => resp.data);
  };

  fetchUniverseList = (): Promise<Universe[]> => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/universes`;
    return axios.get<Universe[]>(requestUrl).then((response) => response.data);
  };

  fetchUniverse = (universeUUID: string | undefined): Promise<Universe> => {
    if (universeUUID) {
      const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/universes/${universeUUID}`;
      return axios.get<Universe>(requestUrl).then((resp) => resp.data);
    }
    return Promise.reject('Failed to fetch universe. No universe UUID provided.');
  };

  fetchUniverseNamespaces = (universeUuid: string | undefined): Promise<UniverseNamespace[]> => {
    if (universeUuid) {
      const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/universes/${universeUuid}/namespaces`;
      return axios.get<UniverseNamespace[]>(requestUrl).then((resp) => resp.data);
    }
    return Promise.reject('Failed to fetch namespaces. No universe UUID provided.');
  };

  createProvider = (
    providerConfigMutation: YBProviderMutation,
    shouldValidate = true,
    ignoreValidationErrors = false
  ) => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/providers`;
    return axios
      .post<YBPTask>(requestUrl, providerConfigMutation, {
        params: {
          validate: shouldValidate,
          ...(shouldValidate && { ignoreValidationErrors: ignoreValidationErrors })
        }
      })
      .then((response) => response.data);
  };

  editProvider = (
    providerUUID: string,
    providerConfigMutation: YBProviderMutation,
    shouldValidate = true,
    ignoreValidationErrors = false
  ) => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/providers/${providerUUID}/edit`;
    return axios
      .put<YBPTask>(requestUrl, providerConfigMutation, {
        params: {
          validate: shouldValidate,
          ...(shouldValidate && { ignoreValidationErrors: ignoreValidationErrors })
        }
      })
      .then((response) => response.data);
  };

  fetchProviderList = (): Promise<YBProvider[]> => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/providers`;
    return axios.get<YBProvider[]>(requestUrl).then((resp) => resp.data);
  };

  deleteProvider = (providerUUID: string) => {
    if (providerUUID) {
      const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/providers/${providerUUID}`;
      return axios.delete<YBPTask>(requestUrl).then((response) => response.data);
    }
    return Promise.reject('Failed to delete provider. No provider UUID provided.');
  };

  /**
   * @Deprecated This function uses an old provider type.
   */
  fetchProviderList_Deprecated = (): Promise<Provider_Deprecated[]> => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/providers`;
    return axios.get<Provider_Deprecated[]>(requestUrl).then((resp) => resp.data);
  };

  fetchProvider = (providerUUID: string | undefined): Promise<YBProvider> => {
    if (providerUUID) {
      const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/providers/${providerUUID}`;
      return axios.get<YBProvider>(requestUrl).then((resp) => resp.data);
    }
    return Promise.reject('Failed to fetch provider. No provider UUID provided.');
  };

  fetchSuggestedKubernetesConfig = () => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/providers/suggested_kubernetes_config`;
    return axios.get<SuggestedKubernetesConfig>(requestUrl).then((response) => response.data);
  };

  fetchProviderRegions = (providerId?: string): Promise<Region[]> => {
    if (providerId) {
      const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/providers/${providerId}/regions`;
      return axios.get<Region[]>(requestUrl).then((resp) => resp.data);
    } else {
      return Promise.reject('Failed to fetch provider regions. No provider UUID provided.');
    }
  };

  fetchRegionMetadata = (
    providerCode: ProviderCode,
    kubernetesProvider?: KubernetesProvider
  ): Promise<RegionMetadataResponse> => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/providers/region_metadata/${providerCode}`;
    return axios
      .get<RegionMetadataResponse>(requestUrl, {
        params: { subType: kubernetesProvider }
      })
      .then((response) => response.data);
  };

  createInstanceType = (providerUUID: string, instanceType: InstanceTypeMutation) => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/providers/${providerUUID}/instance_types`;
    return axios.post<InstanceType>(requestUrl, instanceType).then((response) => response.data);
  };

  fetchInstanceTypes = (providerUUID: string | undefined): Promise<InstanceType[]> => {
    if (providerUUID) {
      const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/providers/${providerUUID}/instance_types`;
      return axios.get<InstanceType[]>(requestUrl).then((response) => response.data);
    } else {
      return Promise.reject('Failed to fetch instance types. No provider UUID provided.');
    }
  };

  deleteInstanceType = (providerUUID: string, instanceTypeCode: string) => {
    if (providerUUID && instanceTypeCode) {
      const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/providers/${providerUUID}/instance_types/${instanceTypeCode}`;
      return axios.delete<YBPSuccess>(requestUrl).then((response) => response.data);
    } else {
      const errorMessage = providerUUID
        ? 'No instance type code provided.'
        : 'No provider UUID provided.';
      return Promise.reject(`Failed to delete instance type. ${errorMessage}`);
    }
  };

  //--------------------------------------------------------------------------------------------
  // Disaster Recovery (DR) API request helpers

  createDrConfig = (createDRConfigRequest: CreateDrConfigRequest): Promise<YBPTask> => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/dr_configs`;
    return axios.post(requestUrl, createDRConfigRequest).then((response) => response.data);
  };

  fetchDrConfig = (drConfigUuid: string | undefined): Promise<DrConfig> => {
    if (drConfigUuid) {
      const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/dr_configs/${drConfigUuid}`;
      return axios.get<DrConfig>(requestUrl).then((response) => response.data);
    } else {
      return Promise.reject('Failed to fetch DR config. No DR config UUID provided.');
    }
  };

  editDrConfig = (
    drConfigUuid: string,
    editDrConfigRequest: EditDrConfigRequest
  ): Promise<YBPTask> => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/dr_configs/${drConfigUuid}/edit`;
    return axios.post<YBPTask>(requestUrl, editDrConfigRequest).then((response) => response.data);
  };

  deleteDrConfig = (drConfigUuid: string, isForceDelete: boolean): Promise<YBPTask> => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/dr_configs/${drConfigUuid}`;
    return axios
      .delete<YBPTask>(requestUrl, { params: { isForceDelete } })
      .then((response) => response.data);
  };

  initiateSwitchover = (
    drConfigUuid: string,
    drSwitchoverRequest: DrSwitchoverRequest
  ): Promise<YBPTask> => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/dr_configs/${drConfigUuid}/switchover`;
    return axios.post<YBPTask>(requestUrl, drSwitchoverRequest).then((response) => response.data);
  };

  initiateFailover = (
    drConfigUuid: string,
    drFailoverRequest: DrFailoverRequest
  ): Promise<YBPTask> => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/dr_configs/${drConfigUuid}/failover`;
    return axios.post<YBPTask>(requestUrl, drFailoverRequest).then((response) => response.data);
  };

  replaceDrReplica = (
    drConfigUuid: string,
    replaceDrReplicaRequest: ReplaceDrReplicaRequest
  ): Promise<YBPTask> => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/dr_configs/${drConfigUuid}/replace_replica`;
    return axios.post(requestUrl, replaceDrReplicaRequest).then((response) => response.data);
  };

  fetchCurrentSafetimes = (drConfigUuid: string): Promise<DrConfigSafetimeResponse> => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/dr_configs/${drConfigUuid}/safetime`;
    return axios.get<DrConfigSafetimeResponse>(requestUrl).then((response) => response.data);
  };

  // The following DR API request helpers manage the underlying replication used in the DR config.

  restartDrConfig = (drConfigUuid: string, restartDrConfigRequest: RestartDrConfigRequest) => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/dr_configs/${drConfigUuid}/restart`;
    return axios
      .post<YBPTask>(requestUrl, restartDrConfigRequest)
      .then((response) => response.data);
  };

  updateTablesInDr = (drConfigUuid: string, updateTablesInDrRequest: UpdateTablesInDrRequest) => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/dr_configs/${drConfigUuid}/set_tables`;
    updateTablesInDrRequest.autoIncludeIndexTables =
      updateTablesInDrRequest.autoIncludeIndexTables ?? true;
    return axios
      .post<YBPTask>(requestUrl, updateTablesInDrRequest)
      .then((response) => response.data);
  };

  syncDrConfig = (drConfigUuid: string) => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/dr_configs/${drConfigUuid}/sync`;
    return axios.post<YBPTask>(requestUrl).then((response) => response.data);
  };

  //--------------------------------------------------------------------------------------------

  abortTask = (taskUuid: string) => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/task/${taskUuid}/abort`;
    return axios.post<YBPSuccess>(requestUrl).then((response) => response.data);
  };

  getAZList = (providerId: string, regionId: string): Promise<AvailabilityZone[]> => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/providers/${providerId}/regions/${regionId}/zones`;
    return axios.get<AvailabilityZone[]>(requestUrl).then((resp) => resp.data);
  };

  universeConfigure = (data: UniverseConfigure): Promise<UniverseDetails> => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/universe_configure`;
    return axios.post<UniverseDetails>(requestUrl, data).then((resp) => resp.data);
  };

  universeCreate = (data: UniverseConfigure): Promise<Universe> => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/universes`;
    return axios.post<Universe>(requestUrl, data).then((resp) => resp.data);
  };

  universeEdit = (data: UniverseConfigure, universeId: string): Promise<Universe> => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/universes/${universeId}`;
    return axios.put<Universe>(requestUrl, data).then((resp) => resp.data);
  };

  getDBVersions = (): Promise<string[]> => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/releases`;
    return axios.get<string[]>(requestUrl).then((resp) => resp.data);
  };

  getDBVersionsByProvider = (providerId?: string): Promise<string[]> => {
    if (providerId) {
      const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/providers/${providerId}/releases`;
      return axios.get<string[]>(requestUrl).then((resp) => resp.data);
    } else {
      return Promise.reject('Querying access keys failed. No provider ID provided');
    }
  };

  getAccessKeys = (providerId?: string): Promise<AccessKey[]> => {
    if (providerId) {
      const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/providers/${providerId}/access_keys`;
      return axios.get<AccessKey[]>(requestUrl).then((resp) => resp.data);
    } else {
      return Promise.reject('Querying access keys failed. No provider ID provided');
    }
  };

  getCertificates = (): Promise<Certificate[]> => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/certificates`;
    return axios.get<Certificate[]>(requestUrl).then((resp) => resp.data);
  };

  getKMSConfigs = (): Promise<KmsConfig[]> => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/kms_configs`;
    return axios.get<KmsConfig[]>(requestUrl).then((resp) => resp.data);
  };

  getHAConfig = (): Promise<HaConfig> => {
    const requestUrl = `${ROOT_URL}/settings/ha/config`;
    return axios.get<HaConfig>(requestUrl).then((resp) => resp.data);
  };

  createHAConfig = (createHaConfigRequest: CreateHaConfigRequest): Promise<HaConfig> => {
    const requestUrl = `${ROOT_URL}/settings/ha/config`;
    return axios
      .post<HaConfig>(requestUrl, createHaConfigRequest)
      .then((response) => response.data);
  };

  editHAConfig = (
    haConfigUuid: string,
    editHaConfigRequest: EditHaConfigRequest
  ): Promise<HaConfig> => {
    const requestUrl = `${ROOT_URL}/settings/ha/config/${haConfigUuid}`;
    return axios.put<HaConfig>(requestUrl, editHaConfigRequest).then((response) => response.data);
  };

  deleteHAConfig = (haConfigUuid: string): Promise<void> => {
    const requestUrl = `${ROOT_URL}/settings/ha/config/${haConfigUuid}`;
    return axios.delete(requestUrl);
  };

  createHAInstance = (
    haConfigUuid: string,
    address: string,
    isLeader: boolean,
    isLocal: boolean
  ): Promise<HaPlatformInstance> => {
    const requestUrl = `${ROOT_URL}/settings/ha/config/${haConfigUuid}/instance`;
    const payload = {
      address,
      is_leader: isLeader,
      is_local: isLocal
    };
    return axios.post<HaPlatformInstance>(requestUrl, payload).then((resp) => resp.data);
  };

  deleteHAInstance = (haConfigUuid: string, instanceId: string): Promise<void> => {
    const requestUrl = `${ROOT_URL}/settings/ha/config/${haConfigUuid}/instance/${instanceId}`;
    return axios.delete(requestUrl);
  };

  promoteHAInstance = (
    haConfigUuid: string,
    instanceId: string,
    isForcePromote: boolean,
    promoteHaInstanceRequest: PromoteHaInstanceRequest
  ): Promise<void> => {
    const requestUrl = `${ROOT_URL}/settings/ha/config/${haConfigUuid}/instance/${instanceId}/promote`;
    return axios.post(requestUrl, promoteHaInstanceRequest, { params: { isForcePromote } });
  };

  getHABackups = (haConfigUuid: string): Promise<string[]> => {
    const requestUrl = `${ROOT_URL}/settings/ha/config/${haConfigUuid}/backup/list`;
    return axios.get<string[]>(requestUrl).then((resp) => resp.data);
  };

  getHAReplicationSchedule = (haConfigUuid?: string): Promise<HaReplicationSchedule> => {
    if (haConfigUuid) {
      const requestUrl = `${ROOT_URL}/settings/ha/config/${haConfigUuid}/replication_schedule`;
      return axios.get<HaReplicationSchedule>(requestUrl).then((resp) => resp.data);
    } else {
      return Promise.reject('Querying HA replication schedule failed. No config ID provided');
    }
  };

  startHABackupSchedule = (
    haConfigUuid?: string,
    replicationFrequency?: number
  ): Promise<HaReplicationSchedule> => {
    if (haConfigUuid && replicationFrequency) {
      const requestUrl = `${ROOT_URL}/settings/ha/config/${haConfigUuid}/replication_schedule/start`;
      const payload = { frequency_milliseconds: replicationFrequency };
      return axios.put<HaReplicationSchedule>(requestUrl, payload).then((resp) => resp.data);
    } else {
      return Promise.reject(
        'Start HA backup schedule failed. No config ID or replication frequency provided'
      );
    }
  };

  stopHABackupSchedule = (haConfigUuid: string): Promise<HaReplicationSchedule> => {
    const requestUrl = `${ROOT_URL}/settings/ha/config/${haConfigUuid}/replication_schedule/stop`;
    return axios.put<HaReplicationSchedule>(requestUrl).then((resp) => resp.data);
  };

  generateHAKey = (): Promise<Pick<HaConfig, 'cluster_key'>> => {
    const requestUrl = `${ROOT_URL}/settings/ha/generate_key`;
    return axios.get<Pick<HaConfig, 'cluster_key'>>(requestUrl).then((resp) => resp.data);
  };

  // check if exception was caused by canceling previous request
  isRequestCancelError(error: unknown): boolean {
    return axios.isCancel(error);
  }

  /**
   * Delete certificate which is not attched to any universe.
   *
   * @param certUUID - certificate UUID
   */
  deleteCertificate = (certUUID: string, customerUUID: string): Promise<any> => {
    const requestUrl = `${ROOT_URL}/customers/${customerUUID}/certificates/${certUUID}`;
    return axios.delete<any>(requestUrl).then((res) => res.data);
  };

  fetchUniverseTasks = (universeUuid: string): Promise<any> => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/tasks_list`;
    return axios
      .get<any>(requestUrl, { params: { uUUID: universeUuid } })
      .then((response) => response.data);
  };

  getAlerts = (
    offset: number,
    limit: number,
    sortBy: string,
    direction = 'ASC',
    filter: {}
  ): Promise<any> => {
    const payload = {
      filter,
      sortBy,
      direction,
      offset,
      limit,
      needTotalCount: true
    };

    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/alerts/page`;
    return axios.post(requestUrl, payload).then((res) => res.data);
  };

  getAlertCount = (filter: {}): Promise<any> => {
    const payload = {
      ...filter
    };
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/alerts/count`;
    return axios.post(requestUrl, payload).then((res) => res.data);
  };

  getAlert = (alertUUID: string) => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/alerts/${alertUUID}`;
    return axios.get(requestUrl).then((res) => res.data);
  };

  acknowledgeAlert = (uuid: string) => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/alerts/acknowledge`;
    return axios.post(requestUrl, { uuids: [uuid] }).then((res) => res.data);
  };

  fetchMetrics = (metricsQueryParams: MetricsQueryParams): Promise<MetricsQueryResponse> => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/metrics`;
    return axios
      .post<MetricsQueryResponse>(requestUrl, metricsQueryParams)
      .then((response) => response.data);
  };

  importReleases = (payload: any) => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/releases`;
    return axios.post(requestUrl, payload).then((res) => res.data);
  };
}

export const api = new ApiService();
