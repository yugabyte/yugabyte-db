import axios from 'axios';

import { ROOT_URL } from '../config';
import { Provider } from '../redesign/helpers/dtos';
import { DEFAULT_RUNTIME_GLOBAL_SCOPE } from '../actions/customers';

export function fetchCustomersList() {
  return axios.get(`${ROOT_URL}/customers`);
}

export function fetchGlobalRunTimeConfigs(includeInherited = false) {
  const cUUID = localStorage.getItem('customerId');
  return axios.get(
    `${ROOT_URL}/customers/${cUUID}/runtime_config/${DEFAULT_RUNTIME_GLOBAL_SCOPE}?includeInherited=${includeInherited}`
  );
}

export function fetchProviderList() {
  const cUUID = localStorage.getItem('customerId');
  return axios.get<Provider>(`${ROOT_URL}/customers/${cUUID}/providers`);
}
