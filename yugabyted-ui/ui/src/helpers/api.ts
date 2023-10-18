import type { AxiosError } from 'axios';
// Important: to avoid accidental circular dependencies imports inside @app/helpers must happen via full path
import { browserStorage } from '@app/helpers/browserStorage';
import { browserHistory } from '@app/helpers/browserHistory';
import { AXIOS_INSTANCE } from '@app/api/src';
import config from '@app/config.json';

export const API_URL = config.api_url;

// init main http client instance
AXIOS_INSTANCE.defaults.withCredentials = true;
AXIOS_INSTANCE.defaults.baseURL = API_URL;
AXIOS_INSTANCE.defaults.paramsSerializer = (params: Record<string, unknown>) => {
  return Object.keys(params)
    .map((key) => {
      // eslint-disable-next-line eqeqeq
      if (params[key] == undefined) {
        return null;
      }
      if (Array.isArray(params[key])) {
        return `${key}=${(params[key] as string[]).map(encodeURIComponent).join(',')}`;
      }
      if (params[key] instanceof Set) {
        return `${key}=${[...(params[key] as string[])].map(encodeURIComponent).join(',')}`;
      }
      return `${key}=${encodeURIComponent(String(params[key]))}`;
    })
    .filter(Boolean)
    .join('&');
};

if (browserStorage.authToken) {
  AXIOS_INSTANCE.defaults.headers.common['Authorization'] = `Bearer ${browserStorage.authToken}`;
}

// globally catch unauthorized responses and redirect to the login page
const UNAUTHORISED_CODES = new Set([401]);

AXIOS_INSTANCE.interceptors.response.use(
  (response) => response,
  (error: AxiosError) => {

    if (
      error.response &&
      UNAUTHORISED_CODES.has(error.response.status) &&
      localStorage.getItem("rbac_enabled") !== 'true') {
      // For now we need signup page to be primary page so changing it to redirect to sign.
      // will need to change it back to login once we have our own login page.
      browserHistory.redirectToLogin(true);
    }
    return Promise.reject(error);
  }
);
