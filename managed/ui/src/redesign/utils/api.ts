import axios, { Canceler } from 'axios';
import { ROOT_URL } from '../../config';
import { KMSRotationHistory } from '../features/universe/universe-actions/encryption-at-rest/EncryptionAtRestUtils';
import {
  Universe,
  KmsConfig,
  EncryptionAtRestConfig
} from '../features/universe/universe-form/utils/dto';

// define unique names to use them as query keys
export enum QUERY_KEY {
  fetchUniverse = 'fetchUniverse',
  getKMSConfigs = 'getKMSConfigs',
  getKMSHistory = 'getKMSHistory',
  setKMSConfig = 'setKMSConfig'
}

class ApiService {
  private cancellers: Record<string, Canceler> = {};

  // check if exception was caused by canceling previous request
  isRequestCancelError(error: unknown): boolean {
    return axios.isCancel(error);
  }

  //apis
  private getCustomerId(): string {
    const customerId = localStorage.getItem('customerId');
    return customerId || '';
  }

  fetchUniverse = (universeId: string): Promise<Universe> => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/universes/${universeId}`;
    return axios.get<Universe>(requestUrl).then((resp) => resp.data);
  };

  setKMSConfig = (universeId: string, data: EncryptionAtRestConfig): Promise<Universe> => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/universes/${universeId}/set_key`;
    return axios.post<Universe>(requestUrl, data).then((resp) => resp.data);
  };

  getKMSConfigs = (): Promise<KmsConfig[]> => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/kms_configs`;
    return axios.get<KmsConfig[]>(requestUrl).then((resp) => resp.data);
  };

  getKMSHistory = (universeId: string): Promise<KMSRotationHistory> => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/universes/${universeId}/kms`;
    return axios.get<KMSRotationHistory>(requestUrl).then((resp) => resp.data);
  };
}

export const api = new ApiService();
