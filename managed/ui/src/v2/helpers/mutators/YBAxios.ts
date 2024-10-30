import axios from 'axios';
import { AxiosError, AxiosRequestConfig } from 'axios';

export const IN_DEVELOPMENT_MODE = process.env.NODE_ENV === 'development';

// if we export the ROOT_URL from the config.js file, orval is trying to parse it and throwing an error.
// so we are copy pasting it here
export const ROOT_URL =
  process.env.REACT_APP_YUGAWARE_API_URL ??
  (IN_DEVELOPMENT_MODE ? 'http://localhost:9000/api/v2' : '/api/v2');

const URLWithRemovedSubPath = ROOT_URL.replace("/api/v1", "/api/v2");

// add a second `options` argument here if you want to pass extra options to each generated query
export const YBAxiosInstance = <T>(
  config: AxiosRequestConfig,
  options?: AxiosRequestConfig,
): Promise<T> => {
  const promise = axios({ ...config, url: `${URLWithRemovedSubPath}${config.url}`, ...options }).then(({ data }) => data);
  return promise;
};

export type ErrorType<Error> = AxiosError<Error>;
