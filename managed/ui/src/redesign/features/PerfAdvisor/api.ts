import axios from 'axios';
import { ROOT_URL } from '../../../config';

export enum QUERY_KEY {
  fetchPerfAdvisorList = 'fetchPerfAdvisorList',
  fetchPerfAdvisorUuid = 'fetchPerfAdvisorUuid',
  registerYBAToPerfAdvisor = 'registerYBAToPerfAdvisor',
  updatePerfAdvisorMetadata = 'updatePerfAdvisorMetadata',
  unRegisterPerfAdvisor = 'unRegisterPerfAdvisor',
  fetchUniverseRegistrationDetails = 'fetchUniverseRegistrationDetails',
  attachUniverseToPerfAdvisor = 'attachUniverseToPerfAdvisor',
  deleteUniverseRegistration = 'deleteUniverseRegistration'
}

export const AXIOS_INSTANCE = axios.create({ baseURL: ROOT_URL, withCredentials: true });

class ApiService {
  private getCustomerId(): string {
    const customerId = localStorage.getItem('customerId');
    return customerId ?? '';
  }

  // Fetches list of all Perf Advisor services
  fetchPerfAdvisorList = () => {
    const requestURL = `${ROOT_URL}/customers/${this.getCustomerId()}/pa_collector`;
    return axios.get(requestURL).then((res) => res.data);
  };

  // Fetches info about specific Perf Advisor service
  fetchPerfAdvisorUuid = (paUuid: string) => {
    const requestURL = `${ROOT_URL}/customers/${this.getCustomerId()}/pa_collector/${paUuid}`;
    return axios.get(requestURL).then((res) => res.data);
  };

  // Register current YBA (customer) to a Perf Advisor service
  registerYBAToPerfAdvisor = (
    paUrl: string,
    ybaUrl: string,
    metricsUrl: string,
    metricsUsername: string,
    metricsPassword: string,
    apiToken: string,
    tpApiToken: string,
    metricsScrapePeriodSecs: number
  ) => {
    const requestURL = `${ROOT_URL}/customers/${this.getCustomerId()}/pa_collector`;
    return axios
      .post(requestURL, {
        customerUUID: this.getCustomerId(),
        paUrl,
        ybaUrl,
        metricsUrl,
        metricsUsername,
        metricsPassword,
        apiToken,
        tpApiToken,
        metricsScrapePeriodSecs
      })
      .then((res) => res.data);
  };

  // Edit/Update metadata about Perf Advisor service
  updatePerfAdvisorMetadata = (data: any, paUuid: string, forceUpdate: boolean) => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/pa_collector/${paUuid}`;
    const params = {
      force: forceUpdate
    };
    return axios
      .put(requestUrl, data, {
        params: params
      })
      .then((resp) => resp.data);
  };

  // Unregister YBA (customer) from Perf Advisor service
  unRegisterPerfAdvisor = (paUuid: string, forceUnregister: boolean) => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/pa_collector/${paUuid}`;
    const params = {
      force: forceUnregister
    };
    return axios
      .delete(requestUrl, {
        params: params
      })
      .then((resp) => resp.data);
  };

  // Fetch Perf Advisor service registration details for a universe
  fetchUniverseRegistrationDetails = (universeUuid: string) => {
    const requestURL = `${ROOT_URL}/customers/${this.getCustomerId()}/universes/${universeUuid}/pa_collector`;
    return axios.get(requestURL).then((res) => res.data);
  };

  // Enable Perf Advisor for current universe
  attachUniverseToPerfAdvisor = (paUuid: string, universeUuid: string) => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/universes/${universeUuid}/pa_collector/${paUuid}`;
    return axios.put(requestUrl).then((resp) => resp.data);
  };

  // Disable Perf Advisor for current universe
  deleteUniverseRegistration = (universeUuid: string) => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/universes/${universeUuid}/pa_collector`;
    return axios.delete(requestUrl).then((resp) => resp.data);
  };
}

export const PerfAdvisorAPI = new ApiService();
