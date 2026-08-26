import axios, { AxiosError, AxiosRequestConfig } from 'axios';

export const IN_DEVELOPMENT_MODE = import.meta.env.DEV;

// When VITE_YUGAWARE_API_URL is set (see `npm run start:remote`), API calls flow through the Vite
// dev-server proxy (/api -> remote YBA), so use a relative root to keep requests same-origin.
const USE_DEV_PROXY = Boolean(import.meta.env.VITE_YUGAWARE_API_URL);

// Plain `npm start` (no VITE_YUGAWARE_API_URL): the SPA talks directly to a local backend on :9000.
const USE_LOCAL_BACKEND = IN_DEVELOPMENT_MODE && !USE_DEV_PROXY;

// if we export the ROOT_URL from the config.js file, orval is trying to parse it and throwing an error.
// so we are copy pasting it here
export const ROOT_URL = USE_LOCAL_BACKEND ? 'http://localhost:9000/api/v2' : '/api/v2';

export const URLWithRemovedSubPath = ROOT_URL.replace('/api/v1', '/api/v2');

// add a second `options` argument here if you want to pass extra options to each generated query
export const YBAxiosInstance = <T>(
  config: AxiosRequestConfig,
  options?: AxiosRequestConfig
): Promise<T> => {
  const promise = axios({
    ...config,
    url: `${URLWithRemovedSubPath}${config.url}`,
    ...options
  }).then(({ data }) => data);
  return promise;
};

export type ErrorType<Error> = AxiosError<Error>;
