import axios from 'axios';
import { ROOT_URL } from './helpers/config';
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
    const requestURL = `${ROOT_URL}/anomalies`;
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
    const requestURL = `${ROOT_URL}/anomalies/${anomalyUuid}`;
    const params = {
      universe_uuid: universeUuid
    }
    return axios.get<Anomaly>(requestURL, {
      params: params}).then((res) => res.data);
  };

  // Fetches graphs and supporting data for troubleshooting 
  fetchGraphs = (universeUuid: String, data: any) => {
    const requestUrl = `${ROOT_URL}/graphs`;
    return axios.post<GraphResponse[]>(requestUrl, data, {
      params: {
        universe_uuid: universeUuid
        // mocked: true
      }
    }).then((resp) => resp.data);
  };
}

export const TroubleshootAPI = new ApiService();
