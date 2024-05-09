import axios from 'axios';
import { IN_DEVELOPMENT_MODE, ROOT_URL } from './helpers/config';
import { Anomaly, GraphResponse } from './helpers/dtos';

// define unique names to use them as query keys
export enum QUERY_KEY {
  fetchAnamolies = 'fetchAnamolies',
  fetchGraphs = 'fetchGraphs',
}

class ApiService {
  // private getCustomerId(): string {
  //   const customerId = localStorage.getItem('customerId');
  //   return customerId ?? '';
  // }

  // Fetches list of anomalies
  fetchAnamolies = (universeUuid: string, startTime?: Date | null, endTime?: Date | null) => {
    const requestURL = IN_DEVELOPMENT_MODE ? 'http://localhost:8080/anomalies' : 'https://10.9.15.156:8443/anomalies';
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

  fetchAnamoliesById = (universeUuid: string, anomalyUuid: string) => {
    console.warn('process.env.REACT_APP_YUGAWARE_API_URL', process?.env?.REACT_APP_YUGAWARE_API_URL);
    console.warn('IN_DEVELOPMENT_MODE', IN_DEVELOPMENT_MODE);
    const requestURL = IN_DEVELOPMENT_MODE ? `http://localhost:8080/anomalies/${anomalyUuid}` : `https://10.9.15.156:8443/anomalies/${anomalyUuid}`;
    const params = {
      universe_uuid: universeUuid
    }
    return axios.get<Anomaly>(requestURL, {
      params: params}).then((res) => res.data);
  };

  // Fetches graphs and supporting data for troubleshooting 
  fetchGraphs = (universeUuid: String, data: any) => {
    const requestUrl = IN_DEVELOPMENT_MODE ? 'http://localhost:8080/graphs' : 'https://10.9.15.156:8443/graphs';
    return axios.post<GraphResponse[]>(requestUrl, data, {
      params: {
        universe_uuid: universeUuid
        // mocked: true
      }
    }).then((resp) => resp.data);
  };
}

export const TroubleshootAPI = new ApiService();
