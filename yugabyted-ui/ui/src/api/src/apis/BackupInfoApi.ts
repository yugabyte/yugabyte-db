// tslint:disable
/**
 * Yugabyte Cloud
 * YugabyteDB as a Service
 *
 * The version of the OpenAPI document: v1
 * Contact: support@yugabyte.com
 *
 * NOTE: This class is auto generated by OpenAPI Generator (https://openapi-generator.tech).
 * https://openapi-generator.tech
 * Do not edit the class manually.
 */

// eslint-disable-next-line @typescript-eslint/ban-ts-comment
// @ts-ignore
import { useQuery, useInfiniteQuery, useMutation, UseQueryOptions, UseInfiniteQueryOptions, UseMutationOptions } from 'react-query';
import Axios from '../runtime';
import type { AxiosInstance } from 'axios';
// eslint-disable-next-line @typescript-eslint/ban-ts-comment
// @ts-ignore
import type {
  ApiError,
  InlineResponse2001,
} from '../models';


/**
 * Retrieve the list of databases on which backup is enabled in the YugabyteDB cluster.
 * Get Backup Details
 */

export const getBackupDetailsAxiosRequest = (
  customAxiosInstance?: AxiosInstance
) => {
  return Axios<InlineResponse2001>(
    {
      url: '/backup',
      method: 'GET',
      params: {
      }
    },
    customAxiosInstance
  );
};

export const getBackupDetailsQueryKey = (
  pageParam = -1,
  version = 1,
) => [
  `/v${version}/backup`,
  pageParam,
];


export const useGetBackupDetailsInfiniteQuery = <T = InlineResponse2001, Error = ApiError>(
  options?: {
    query?: UseInfiniteQueryOptions<InlineResponse2001, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  pageParam = -1,
  version = 1,
) => {
  const queryKey = getBackupDetailsQueryKey(pageParam, version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useInfiniteQuery<InlineResponse2001, Error, T>(
    queryKey,
    () => getBackupDetailsAxiosRequest(customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};

export const useGetBackupDetailsQuery = <T = InlineResponse2001, Error = ApiError>(
  options?: {
    query?: UseQueryOptions<InlineResponse2001, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  version = 1,
) => {
  const queryKey = getBackupDetailsQueryKey(version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useQuery<InlineResponse2001, Error, T>(
    queryKey,
    () => getBackupDetailsAxiosRequest(customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};





