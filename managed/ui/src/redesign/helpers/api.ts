import axios, { Canceler } from 'axios';
import { ROOT_URL } from '../../config';
import {
  AvailabilityZone,
  Provider,
  Region,
  Universe,
  UniverseDetails,
  InstanceType,
  AccessKey,
  Certificate,
  KmsConfig,
  UniverseConfigure,
  HAConfig,
  HAReplicationSchedule,
  HAPlatformInstance
} from './dtos';

// define unique names to use them as query keys
export enum QUERY_KEY {
  fetchUniverse = 'fetchUniverse',
  getProvidersList = 'getProvidersList',
  getRegionsList = 'getRegionsList',
  universeConfigure = 'universeConfigure',
  getInstanceTypes = 'getInstanceTypes',
  getDBVersions = 'getDBVersions',
  getAccessKeys = 'getAccessKeys',
  getCertificates = 'getCertificates',
  getKMSConfigs = 'getKMSConfigs',
  deleteCertificate = 'deleteCertificate',
  getHAConfig = 'getHAConfig',
  getHAReplicationSchedule = 'getHAReplicationSchedule',
  getHABackups = 'getHABackups'
}

class ApiService {
  private cancellers: Record<string, Canceler> = {};

  private getCustomerId(): string {
    const customerId = localStorage.getItem('customerId');
    return customerId || '';
  }

  findUniverseByName = (universeName: string): Promise<Universe> => {
    // auto-cancel previous request, if any
    if (this.cancellers.findUniverseByName) this.cancellers.findUniverseByName();

    // update cancellation stuff
    const source = axios.CancelToken.source();
    this.cancellers.findUniverseByName = source.cancel;

    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/universes/find/${universeName}`;
    return axios
      .get<Universe>(requestUrl, { cancelToken: source.token })
      .then((resp) => resp.data);
  };

  fetchUniverse = (universeId: string): Promise<Universe> => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/universes/${universeId}`;
    return axios.get<Universe>(requestUrl).then((resp) => resp.data);
  };

  getProvidersList = (): Promise<Provider[]> => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/providers`;
    return axios.get<Provider[]>(requestUrl).then((resp) => resp.data);
  };

  getRegionsList = (providerId?: string): Promise<Region[]> => {
    if (providerId) {
      const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/providers/${providerId}/regions`;
      return axios.get<Region[]>(requestUrl).then((resp) => resp.data);
    } else {
      return Promise.reject('Querying regions failed: no provider ID provided');
    }
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

  getInstanceTypes = (providerId?: string): Promise<InstanceType[]> => {
    if (providerId) {
      const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/providers/${providerId}/instance_types`;
      return axios.get<InstanceType[]>(requestUrl).then((resp) => resp.data);
    } else {
      return Promise.reject('Querying instance types failed: no provider ID provided');
    }
  };

  getDBVersions = (): Promise<string[]> => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/releases`;
    return axios.get<string[]>(requestUrl).then((resp) => resp.data);
  };

  getAccessKeys = (providerId?: string): Promise<AccessKey[]> => {
    if (providerId) {
      const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/providers/${providerId}/access_keys`;
      return axios.get<AccessKey[]>(requestUrl).then((resp) => resp.data);
    } else {
      return Promise.reject('Querying access keys failed: no provider ID provided');
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

  createHAConfig = (clusterKey: string): Promise<HAConfig> => {
    const requestUrl = `${ROOT_URL}/settings/ha/config`;
    const payload = { cluster_key: clusterKey };
    return axios.post<HAConfig>(requestUrl, payload).then((resp) => resp.data);
  };

  getHAConfig = (): Promise<HAConfig> => {
    const requestUrl = `${ROOT_URL}/settings/ha/config`;
    return axios.get<HAConfig>(requestUrl).then((resp) => resp.data);
  };

  deleteHAConfig = (configId: string): Promise<void> => {
    const requestUrl = `${ROOT_URL}/settings/ha/config/${configId}`;
    return axios.delete(requestUrl);
  };

  createHAInstance = (
    configId: string,
    address: string,
    isLeader: boolean,
    isLocal: boolean
  ): Promise<HAPlatformInstance> => {
    const requestUrl = `${ROOT_URL}/settings/ha/config/${configId}/instance`;
    const payload = {
      address,
      is_leader: isLeader,
      is_local: isLocal
    };
    return axios.post<HAPlatformInstance>(requestUrl, payload).then((resp) => resp.data);
  };

  deleteHAInstance = (configId: string, instanceId: string): Promise<void> => {
    const requestUrl = `${ROOT_URL}/settings/ha/config/${configId}/instance/${instanceId}`;
    return axios.delete(requestUrl);
  };

  promoteHAInstance = (configId: string, instanceId: string, backupFile: string): Promise<void> => {
    const requestUrl = `${ROOT_URL}/settings/ha/config/${configId}/instance/${instanceId}/promote`;
    const payload = { backup_file: backupFile };
    return axios.post(requestUrl, payload);
  };

  getHABackups = (configId: string): Promise<string[]> => {
    const requestUrl = `${ROOT_URL}/settings/ha/config/${configId}/backup/list`;
    return axios.get<string[]>(requestUrl).then((resp) => resp.data);
  };

  getHAReplicationSchedule = (configId?: string): Promise<HAReplicationSchedule> => {
    if (configId) {
      const requestUrl = `${ROOT_URL}/settings/ha/config/${configId}/replication_schedule`;
      return axios.get<HAReplicationSchedule>(requestUrl).then((resp) => resp.data);
    } else {
      return Promise.reject('Querying HA replication schedule failed: no config ID provided');
    }
  };

  startHABackupSchedule = (
    configId?: string,
    replicationFrequency?: number
  ): Promise<HAReplicationSchedule> => {
    if (configId && replicationFrequency) {
      const requestUrl = `${ROOT_URL}/settings/ha/config/${configId}/replication_schedule/start`;
      const payload = { frequency_milliseconds: replicationFrequency };
      return axios.put<HAReplicationSchedule>(requestUrl, payload).then((resp) => resp.data);
    } else {
      return Promise.reject(
        'Start HA backup schedule failed: no config ID or replication frequency provided'
      );
    }
  };

  stopHABackupSchedule = (configId: string): Promise<HAReplicationSchedule> => {
    const requestUrl = `${ROOT_URL}/settings/ha/config/${configId}/replication_schedule/stop`;
    return axios.put<HAReplicationSchedule>(requestUrl).then((resp) => resp.data);
  };

  generateHAKey = (): Promise<Pick<HAConfig, 'cluster_key'>> => {
    const requestUrl = `${ROOT_URL}/settings/ha/generate_key`;
    return axios.get<Pick<HAConfig, 'cluster_key'>>(requestUrl).then((resp) => resp.data);
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
    const requestUrl = `${ROOT_URL}/customers/${customerUUID}/certificates/${certUUID}`
    return axios.delete<any>(requestUrl).then((res) => res.data);
  }

  getAlerts = (offset: number, limit: number, sortBy: string, direction = "ASC",  filter: {}): Promise<any> => {

    const payload = {
      filter,
      sortBy,
      direction,
      offset,
      limit,
      "needTotalCount": true
    }

    const requestURL = `${ROOT_URL}/customers/${this.getCustomerId()}/alerts/page`;
    return axios.post(requestURL, payload).then((res) => res.data);
  }

  getAlertCount = (filter: {}): Promise<any> => {
    const payload = {
      ...filter
    }
    const requestURL = `${ROOT_URL}/customers/${this.getCustomerId()}/alerts/count`;
    return axios.post(requestURL, payload).then(res => res.data);
  }

  getAlert = (alertUUID: string) => {
    const requestURL = `${ROOT_URL}/customers/${this.getCustomerId()}/alerts/${alertUUID}`;
    return axios.get(requestURL).then((res) => res.data);
  }

  acknowledgeAlert = (uuid: string) => {
    const requestURL = `${ROOT_URL}/customers/${this.getCustomerId()}/alerts/acknowledge`;
    return axios.post(requestURL, { uuids: [uuid] }).then((res) => res.data);
  }
}

export const api = new ApiService();
