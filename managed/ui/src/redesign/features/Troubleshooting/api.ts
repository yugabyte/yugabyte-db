import axios from 'axios';
import { ROOT_URL } from '../../../config';

export enum QUERY_KEY {
  fetchTpList = 'fetchTpList',
  fetchTpByUuid = 'fetchTpByUuid',
  registerTp = 'registerTp',
  updateTpMetadata = 'updateTpMetadata',
  deleteTp = 'deleteTp',
  fetchUniverseRegistrationDetails = 'fetchUniverseRegistrationDetails',
  monitorUniverse = 'monitorUniverse',
  deleteUniverseRegistration = 'deleteUniverseRegistration'
}

export const AXIOS_INSTANCE = axios.create({ baseURL: ROOT_URL, withCredentials: true });

class ApiService {
  private getCustomerId(): string {
    const customerId = localStorage.getItem('customerId');
    return customerId ?? '';
  }

  // Fetches list of all Troubleshooting Platform services
  fetchTpList = () => {
    const requestURL = `${ROOT_URL}/customers/${this.getCustomerId()}/troubleshooting_platform`;
    return axios.get(requestURL).then((res) => res.data);
  };

  // Fetches info about specific Troubleshooting Platform service
  fetchTpByUuid = (tpUuid: string) => {
    const requestURL = `${ROOT_URL}/customers/${this.getCustomerId()}/troubleshooting_platform/${tpUuid}`;
    return axios.get(requestURL).then((res) => res.data);
  };

  // Register current YBA (customer) to a Troubleshooting Platform service
  registerTp = (tpUrl: string, ybaUrl: string, metricsUrl: string, apiToken: string, metricsScrapePeriodSecs: number) => {
    const requestURL = `${ROOT_URL}/customers/${this.getCustomerId()}/troubleshooting_platform`;
    return axios.post(requestURL,  {
      customerUUID: this.getCustomerId(),
      tpUrl,
      ybaUrl,
      metricsUrl,
      apiToken,
      metricsScrapePeriodSecs
    }).then((res) => res.data);
  };

  // Edit/Update metadata about Troubleshooting Platform service
  updateTpMetadata = (data: any, tpUuid: string, forceUpdate: boolean) => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/troubleshooting_platform/${tpUuid}`;
    const params = {
      force: forceUpdate
    };
    return axios.put(requestUrl, data, {
      params: params
    }).then((resp) => resp.data);
  };

  // Delete(Unregister) Troubleshooting Platform service
  deleteTp = (tpUuid: string, forceUnregister: boolean) => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/troubleshooting_platform/${tpUuid}`;
    const params = {
      force: forceUnregister
    };
    return axios.delete(requestUrl, {
      params: params
    }).then((resp) => resp.data);
  };

  // Fetch registration details of universe to Troubleshooting Platform service
  fetchUniverseRegistrationDetails = (tpUuid: string, universeUuid: string) => {
    const requestURL = `${ROOT_URL}/customers/${this.getCustomerId()}/troubleshooting_platform/${tpUuid}/universes/${universeUuid}`;
    return axios.get(requestURL).then((res) => res.data);
  };

  // Register universe to Troubleshooting Platform service
  monitorUniverse = (tpUuid: string, universeUuid: string) => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/troubleshooting_platform/${tpUuid}/universes/${universeUuid}`;
    return axios.put(requestUrl).then((resp) => resp.data);
  };

  // Delete universe registration
  deleteUniverseRegistration = (tpUuid: string, universeUuid: string) => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/troubleshooting_platform/${tpUuid}/universes/${universeUuid}`;
    return axios.delete(requestUrl).then((resp) => resp.data);
  };
}

export const TroubleshootingAPI = new ApiService();
