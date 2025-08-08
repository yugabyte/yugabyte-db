import axios from 'axios';
import { ROOT_URL } from '@app/v2/helpers/mutators/YBAxios';

export enum V2_QUERY_KEY {
  getFIPSInfo = 'getFIPSInfo'
}
class ApiService {
  getFIPSInfo = (): Promise<any> => {
    const requestUrl = `${ROOT_URL}/yba-info`;
    return axios.get<any>(requestUrl).then((resp) => resp.data);
  };
}

export const apiV2 = new ApiService();
