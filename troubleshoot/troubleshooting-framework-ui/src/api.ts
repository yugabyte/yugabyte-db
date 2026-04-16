import axios from 'axios';
import { IN_DEVELOPMENT_MODE, DEV_HOST_URL, PROD_HOST_URL } from './helpers/config';
import { Anomaly, GraphResponse, MetadataFields } from './helpers/dtos';

// define unique names to use them as query keys
export enum QUERY_KEY {
  fetchAnamolies = 'fetchAnamolies',
  fetchGraphs = 'fetchGraphs',
  fetchQueries = 'fetchQueries',
  fetchUniverseMetadataList = 'fetchUniverseMetadataList',
  deleteUniverseMetadata = 'deleteUniverseMetadata',
  updateUniverseMetadata = 'updateUniverseMetadata',
  fetchUniverseListDetails = 'fetchUniverseListDetails',
  fetchUniverseById = 'fetchUniverseById'
}

class ApiService {
  // Fetches list of anomalies
  fetchAnamolies = (universeUuid: string, startTime?: Date | null, endTime?: Date | null, hostUrl?: string) => {
    const baseUrl = hostUrl ??  IN_DEVELOPMENT_MODE ? DEV_HOST_URL : PROD_HOST_URL;
    const requestURL = `${baseUrl}/anomalies`;
    const params: any = {
      universe_uuid: universeUuid
    }
    if (startTime) {
      params.startTime = startTime;
    }
    if (endTime) {
      params.endTime = endTime;
    }
    return axios.get<Anomaly[]>(requestURL, {
      params: params}).then((res) => res.data);
  };

  fetchAnamoliesById = (universeUuid: string, anomalyUuid: string, hostUrl?: string) => {
    const baseUrl = hostUrl ??  IN_DEVELOPMENT_MODE ? DEV_HOST_URL : PROD_HOST_URL;
    const requestURL = `${baseUrl}/anomalies/${anomalyUuid}`;
    const params = {
      universe_uuid: universeUuid
    }
    return axios.get<Anomaly>(requestURL, {
      params: params}).then((res) => res.data);
  };

  // Fetches graphs and supporting data for troubleshooting 
  fetchGraphs = (universeUuid: String, data: any, hostUrl?: string) => {
    const baseUrl = hostUrl ??  IN_DEVELOPMENT_MODE ? DEV_HOST_URL : PROD_HOST_URL;
    const requestUrl = `${baseUrl}/graphs`;
    return axios.post<GraphResponse[]>(requestUrl, data, {
      params: {
        universe_uuid: universeUuid
        // mocked: true
      }
    }).then((resp) => resp.data);
  };

  fetchQueries = (universeUuid: string, hostUrl?: string) => {
    const baseUrl = hostUrl ??  IN_DEVELOPMENT_MODE ? DEV_HOST_URL : PROD_HOST_URL;
    const requestURL = `${baseUrl}/queries`;
    const params = {
      universe_uuid: universeUuid
    }
    return axios.get<any>(requestURL, {
      params: params}).then((res) => res.data);
  };

  fetchUniverseMetadataList = (hostUrl?: string) => {
    const baseUrl = hostUrl ??  IN_DEVELOPMENT_MODE ? DEV_HOST_URL : PROD_HOST_URL;
    const requestURL = `${baseUrl}/universe_metadata`;
    return axios.get<MetadataFields[]>(requestURL).then((res) => res.data);
  };
  updateUniverseMetadata = (universeUuid: string, data: any, hostUrl?: string) => {
    const baseUrl = hostUrl ??  IN_DEVELOPMENT_MODE ? DEV_HOST_URL : PROD_HOST_URL;
    const requestUrl = `${baseUrl}/universe_metadata/${universeUuid}`;
    return axios.put(requestUrl, data).then((resp) => resp.data);
  };
  deleteUniverseMetadata = (universeUuid: string, hostUrl?: string) => {
    const baseUrl = hostUrl ??  IN_DEVELOPMENT_MODE ? DEV_HOST_URL : PROD_HOST_URL;
    const requestUrl = `${baseUrl}/universe_metadata/${universeUuid}`;
    return axios.delete(requestUrl).then((resp) => resp.data);
  };
  fetchUniverseListDetails = (hostUrl?: string) => {
    const baseUrl = hostUrl ??  IN_DEVELOPMENT_MODE ? DEV_HOST_URL : PROD_HOST_URL;
    const requestUrl = `${baseUrl}/universe_details`;
    return axios.get(requestUrl).then((res) => res.data);
  };
  fetchUniverseById = (universeUuid: string, hostUrl?: string) => {
    const baseUrl = hostUrl ??  IN_DEVELOPMENT_MODE ? DEV_HOST_URL : PROD_HOST_URL;
    const requestUrl = `${baseUrl}/universe_details/${universeUuid}`;
    return axios.get(requestUrl).then((res) => res.data);
  };
}

export const TroubleshootAPI = new ApiService();
