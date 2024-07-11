/**
 * Do not edit manually.
 *
 * YugabyteDB Anywhere V2 APIs
 * An improved set of APIs for managing YugabyteDB Anywhere
 * OpenAPI spec version: v2
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 *
 */
import {
  useQuery,
  useMutation,
  UseQueryOptions,
  UseMutationOptions,
  QueryFunction,
  MutationFunction,
  UseQueryResult,
  QueryKey
} from 'react-query';
import type {
  UniverseRespResponse,
  YBATaskRespResponse,
  UniverseEditReqBody,
  UniverseDeleteReqBody,
  UniverseCreateReqBody,
  ClusterAddReqBody,
  DeleteClusterParams,
  UniverseEditGFlagsReqBody,
  UniverseSoftwareUpgradeReqBody,
  UniverseSoftwareUpgradeFinalizeBody,
  UniverseSoftwareUpgradeFinalizeRespResponse,
  UniverseThirdPartySoftwareUpgradeReqBody,
  UniverseRollbackUpgradeReqBody,
  UniverseSoftwareUpgradePrecheckResponseResponse,
  UniverseSoftwareUpgradePrecheckReqBody,
  UniverseRestartReqBody
} from '../yugabyteDBAnywhereV2APIs.schemas';
import { YBAxiosInstance, ErrorType } from '../../helpers/mutators/YBAxios';

type AsyncReturnType<T extends (...args: any) => Promise<any>> = T extends (
  ...args: any
) => Promise<infer R>
  ? R
  : any;

type SecondParameter<T extends (...args: any) => any> = T extends (
  config: any,
  args: infer P
) => any
  ? P
  : never;

/**
 * Get details of a single YugabyteDB Universe.
 * @summary Get a YugabyteDB Universe
 */
export const getUniverse = (
  uniUUID: string,
  cUUID = localStorage.getItem('customerId')!,
  options?: SecondParameter<typeof YBAxiosInstance>
) => {
  return YBAxiosInstance<UniverseRespResponse>(
    { url: `/customers/${cUUID}/universes/${uniUUID}`, method: 'get' },
    options
  );
};

export const getGetUniverseQueryKey = (
  uniUUID: string,
  cUUID = localStorage.getItem('customerId')!
) => [`/customers/${cUUID}/universes/${uniUUID}`];

export const useGetUniverse = <
  TData = AsyncReturnType<typeof getUniverse>,
  TError = ErrorType<void>
>(
  uniUUID: string,
  cUUID = localStorage.getItem('customerId')!,
  options?: {
    query?: UseQueryOptions<AsyncReturnType<typeof getUniverse>, TError, TData>;
    request?: SecondParameter<typeof YBAxiosInstance>;
  }
): UseQueryResult<TData, TError> & { queryKey: QueryKey } => {
  const { query: queryOptions, request: requestOptions } = options || {};

  const queryKey = queryOptions?.queryKey ?? getGetUniverseQueryKey(uniUUID, cUUID);

  const queryFn: QueryFunction<AsyncReturnType<typeof getUniverse>> = () =>
    getUniverse(uniUUID, cUUID, requestOptions);

  const query = useQuery<AsyncReturnType<typeof getUniverse>, TError, TData>(queryKey, queryFn, {
    enabled: !!(cUUID && uniUUID),
    ...queryOptions
  });

  return {
    queryKey,
    ...query
  };
};

/**
 * Edit the clusters of a single YugabyteDB Universe.
 * @summary Edit a YugabyteDB Universe
 */
export const editUniverse = (
  uniUUID: string,
  universeEditReqBody: UniverseEditReqBody,
  cUUID = localStorage.getItem('customerId')!,
  options?: SecondParameter<typeof YBAxiosInstance>
) => {
  return YBAxiosInstance<YBATaskRespResponse>(
    { url: `/customers/${cUUID}/universes/${uniUUID}`, method: 'put', data: universeEditReqBody },
    options
  );
};

export const useEditUniverse = <TError = ErrorType<void>, TContext = unknown>(options?: {
  mutation?: UseMutationOptions<
    AsyncReturnType<typeof editUniverse>,
    TError,
    { uniUUID: string; data: UniverseEditReqBody; cUUID?: string },
    TContext
  >;
  request?: SecondParameter<typeof YBAxiosInstance>;
}) => {
  const { mutation: mutationOptions, request: requestOptions } = options || {};

  const mutationFn: MutationFunction<
    AsyncReturnType<typeof editUniverse>,
    { uniUUID: string; data: UniverseEditReqBody; cUUID?: string }
  > = (props) => {
    const { uniUUID, data, cUUID } = props || {};

    return editUniverse(uniUUID, data, cUUID, requestOptions);
  };

  return useMutation<
    AsyncReturnType<typeof editUniverse>,
    TError,
    { uniUUID: string; data: UniverseEditReqBody; cUUID?: string },
    TContext
  >(mutationFn, mutationOptions);
};
/**
 * Delete Universe.
 * @summary Delete a universe
 */
export const deleteUniverse = (
  uniUUID: string,
  universeDeleteReqBody: UniverseDeleteReqBody,
  cUUID = localStorage.getItem('customerId')!,
  options?: SecondParameter<typeof YBAxiosInstance>
) => {
  return YBAxiosInstance<YBATaskRespResponse>(
    {
      url: `/customers/${cUUID}/universes/${uniUUID}`,
      method: 'delete',
      data: universeDeleteReqBody
    },
    options
  );
};

export const useDeleteUniverse = <TError = ErrorType<void>, TContext = unknown>(options?: {
  mutation?: UseMutationOptions<
    AsyncReturnType<typeof deleteUniverse>,
    TError,
    { uniUUID: string; data: UniverseDeleteReqBody; cUUID?: string },
    TContext
  >;
  request?: SecondParameter<typeof YBAxiosInstance>;
}) => {
  const { mutation: mutationOptions, request: requestOptions } = options || {};

  const mutationFn: MutationFunction<
    AsyncReturnType<typeof deleteUniverse>,
    { uniUUID: string; data: UniverseDeleteReqBody; cUUID?: string }
  > = (props) => {
    const { uniUUID, data, cUUID } = props || {};

    return deleteUniverse(uniUUID, data, cUUID, requestOptions);
  };

  return useMutation<
    AsyncReturnType<typeof deleteUniverse>,
    TError,
    { uniUUID: string; data: UniverseDeleteReqBody; cUUID?: string },
    TContext
  >(mutationFn, mutationOptions);
};
/**
 * Create all the clusters of a YugabyteDB universe.
 * @summary Create a YugabyteDB Universe
 */
export const createUniverse = (
  universeCreateReqBody: UniverseCreateReqBody,
  cUUID = localStorage.getItem('customerId')!,
  options?: SecondParameter<typeof YBAxiosInstance>
) => {
  return YBAxiosInstance<YBATaskRespResponse>(
    { url: `/customers/${cUUID}/universes`, method: 'post', data: universeCreateReqBody },
    options
  );
};

export const useCreateUniverse = <TError = ErrorType<void>, TContext = unknown>(options?: {
  mutation?: UseMutationOptions<
    AsyncReturnType<typeof createUniverse>,
    TError,
    { data: UniverseCreateReqBody; cUUID?: string },
    TContext
  >;
  request?: SecondParameter<typeof YBAxiosInstance>;
}) => {
  const { mutation: mutationOptions, request: requestOptions } = options || {};

  const mutationFn: MutationFunction<
    AsyncReturnType<typeof createUniverse>,
    { data: UniverseCreateReqBody; cUUID?: string }
  > = (props) => {
    const { data, cUUID } = props || {};

    return createUniverse(data, cUUID, requestOptions);
  };

  return useMutation<
    AsyncReturnType<typeof createUniverse>,
    TError,
    { data: UniverseCreateReqBody; cUUID?: string },
    TContext
  >(mutationFn, mutationOptions);
};
/**
 * Add a cluster (eg. read replica cluster) to a YugabyteDB universe.
 * @summary Add a cluster to a YugabyteDB Universe
 */
export const addCluster = (
  uniUUID: string,
  clusterAddReqBody: ClusterAddReqBody,
  cUUID = localStorage.getItem('customerId')!,
  options?: SecondParameter<typeof YBAxiosInstance>
) => {
  return YBAxiosInstance<YBATaskRespResponse>(
    {
      url: `/customers/${cUUID}/universes/${uniUUID}/clusters`,
      method: 'post',
      data: clusterAddReqBody
    },
    options
  );
};

export const useAddCluster = <TError = ErrorType<void>, TContext = unknown>(options?: {
  mutation?: UseMutationOptions<
    AsyncReturnType<typeof addCluster>,
    TError,
    { uniUUID: string; data: ClusterAddReqBody; cUUID?: string },
    TContext
  >;
  request?: SecondParameter<typeof YBAxiosInstance>;
}) => {
  const { mutation: mutationOptions, request: requestOptions } = options || {};

  const mutationFn: MutationFunction<
    AsyncReturnType<typeof addCluster>,
    { uniUUID: string; data: ClusterAddReqBody; cUUID?: string }
  > = (props) => {
    const { uniUUID, data, cUUID } = props || {};

    return addCluster(uniUUID, data, cUUID, requestOptions);
  };

  return useMutation<
    AsyncReturnType<typeof addCluster>,
    TError,
    { uniUUID: string; data: ClusterAddReqBody; cUUID?: string },
    TContext
  >(mutationFn, mutationOptions);
};
/**
 * Delete an additional cluster (eg. read replica cluster) of a YugabyteDB universe. Primary cluster is deleted along with Universe.
 * @summary Delete an additional cluster(s) of a YugabyteDB Universe
 */
export const deleteCluster = (
  uniUUID: string,
  clsUUID: string,
  params?: DeleteClusterParams,
  cUUID = localStorage.getItem('customerId')!,
  options?: SecondParameter<typeof YBAxiosInstance>
) => {
  return YBAxiosInstance<YBATaskRespResponse>(
    {
      url: `/customers/${cUUID}/universes/${uniUUID}/clusters/${clsUUID}`,
      method: 'delete',
      data: undefined,
      params
    },
    options
  );
};

export const useDeleteCluster = <TError = ErrorType<void>, TContext = unknown>(options?: {
  mutation?: UseMutationOptions<
    AsyncReturnType<typeof deleteCluster>,
    TError,
    { uniUUID: string; clsUUID: string; params?: DeleteClusterParams; cUUID?: string },
    TContext
  >;
  request?: SecondParameter<typeof YBAxiosInstance>;
}) => {
  const { mutation: mutationOptions, request: requestOptions } = options || {};

  const mutationFn: MutationFunction<
    AsyncReturnType<typeof deleteCluster>,
    { uniUUID: string; clsUUID: string; params?: DeleteClusterParams; cUUID?: string }
  > = (props) => {
    const { uniUUID, clsUUID, params, cUUID } = props || {};

    return deleteCluster(uniUUID, clsUUID, params, cUUID, requestOptions);
  };

  return useMutation<
    AsyncReturnType<typeof deleteCluster>,
    TError,
    { uniUUID: string; clsUUID: string; params?: DeleteClusterParams; cUUID?: string },
    TContext
  >(mutationFn, mutationOptions);
};
/**
 * Queues a task to edit GFlags of a universe. The input set of GFlags will replace any existing GFlags in the universe. Refer [YB-Master configuration flags](https://docs.yugabyte.com/preview/reference/configuration/yb-master/#configuration-flags) and [YB-TServer configuration flags](https://docs.yugabyte.com/preview/reference/configuration/yb-tserver/#configuration-flags).
 * @summary Edit GFlags
 */
export const editGFlags = (
  uniUUID: string,
  universeEditGFlagsReqBody: UniverseEditGFlagsReqBody,
  cUUID = localStorage.getItem('customerId')!,
  options?: SecondParameter<typeof YBAxiosInstance>
) => {
  return YBAxiosInstance<YBATaskRespResponse>(
    {
      url: `/customers/${cUUID}/universes/${uniUUID}/gflags`,
      method: 'post',
      data: universeEditGFlagsReqBody
    },
    options
  );
};

export const useEditGFlags = <TError = ErrorType<void>, TContext = unknown>(options?: {
  mutation?: UseMutationOptions<
    AsyncReturnType<typeof editGFlags>,
    TError,
    { uniUUID: string; data: UniverseEditGFlagsReqBody; cUUID?: string },
    TContext
  >;
  request?: SecondParameter<typeof YBAxiosInstance>;
}) => {
  const { mutation: mutationOptions, request: requestOptions } = options || {};

  const mutationFn: MutationFunction<
    AsyncReturnType<typeof editGFlags>,
    { uniUUID: string; data: UniverseEditGFlagsReqBody; cUUID?: string }
  > = (props) => {
    const { uniUUID, data, cUUID } = props || {};

    return editGFlags(uniUUID, data, cUUID, requestOptions);
  };

  return useMutation<
    AsyncReturnType<typeof editGFlags>,
    TError,
    { uniUUID: string; data: UniverseEditGFlagsReqBody; cUUID?: string },
    TContext
  >(mutationFn, mutationOptions);
};
/**
 * Queues a task to perform a YugabyteDB Software upgrade.
 * @summary Upgrade YugabyteDB version
 */
export const startSoftwareUpgrade = (
  uniUUID: string,
  universeSoftwareUpgradeReqBody: UniverseSoftwareUpgradeReqBody,
  cUUID = localStorage.getItem('customerId')!,
  options?: SecondParameter<typeof YBAxiosInstance>
) => {
  return YBAxiosInstance<YBATaskRespResponse>(
    {
      url: `/customers/${cUUID}/universes/${uniUUID}/upgrade/software`,
      method: 'post',
      data: universeSoftwareUpgradeReqBody
    },
    options
  );
};

export const useStartSoftwareUpgrade = <TError = ErrorType<void>, TContext = unknown>(options?: {
  mutation?: UseMutationOptions<
    AsyncReturnType<typeof startSoftwareUpgrade>,
    TError,
    { uniUUID: string; data: UniverseSoftwareUpgradeReqBody; cUUID?: string },
    TContext
  >;
  request?: SecondParameter<typeof YBAxiosInstance>;
}) => {
  const { mutation: mutationOptions, request: requestOptions } = options || {};

  const mutationFn: MutationFunction<
    AsyncReturnType<typeof startSoftwareUpgrade>,
    { uniUUID: string; data: UniverseSoftwareUpgradeReqBody; cUUID?: string }
  > = (props) => {
    const { uniUUID, data, cUUID } = props || {};

    return startSoftwareUpgrade(uniUUID, data, cUUID, requestOptions);
  };

  return useMutation<
    AsyncReturnType<typeof startSoftwareUpgrade>,
    TError,
    { uniUUID: string; data: UniverseSoftwareUpgradeReqBody; cUUID?: string },
    TContext
  >(mutationFn, mutationOptions);
};
/**
 * Queues a task to perform finalize of a YugabyteDB Software upgrade.
 * @summary Finalize the Upgrade YugabyteDB
 */
export const finalizeSoftwareUpgrade = (
  uniUUID: string,
  universeSoftwareUpgradeFinalizeBody: UniverseSoftwareUpgradeFinalizeBody,
  cUUID = localStorage.getItem('customerId')!,
  options?: SecondParameter<typeof YBAxiosInstance>
) => {
  return YBAxiosInstance<YBATaskRespResponse>(
    {
      url: `/customers/${cUUID}/universes/${uniUUID}/upgrade/software/finalize`,
      method: 'post',
      data: universeSoftwareUpgradeFinalizeBody
    },
    options
  );
};

export const useFinalizeSoftwareUpgrade = <TError = ErrorType<void>, TContext = unknown>(options?: {
  mutation?: UseMutationOptions<
    AsyncReturnType<typeof finalizeSoftwareUpgrade>,
    TError,
    { uniUUID: string; data: UniverseSoftwareUpgradeFinalizeBody; cUUID?: string },
    TContext
  >;
  request?: SecondParameter<typeof YBAxiosInstance>;
}) => {
  const { mutation: mutationOptions, request: requestOptions } = options || {};

  const mutationFn: MutationFunction<
    AsyncReturnType<typeof finalizeSoftwareUpgrade>,
    { uniUUID: string; data: UniverseSoftwareUpgradeFinalizeBody; cUUID?: string }
  > = (props) => {
    const { uniUUID, data, cUUID } = props || {};

    return finalizeSoftwareUpgrade(uniUUID, data, cUUID, requestOptions);
  };

  return useMutation<
    AsyncReturnType<typeof finalizeSoftwareUpgrade>,
    TError,
    { uniUUID: string; data: UniverseSoftwareUpgradeFinalizeBody; cUUID?: string },
    TContext
  >(mutationFn, mutationOptions);
};
/**
 * Get finalize info of a YugabyteDB Software upgrade.
 * @summary Get finalize information on the YugabyteDB upgrade
 */
export const getFinalizeSoftwareUpgradeInfo = (
  uniUUID: string,
  cUUID = localStorage.getItem('customerId')!,
  options?: SecondParameter<typeof YBAxiosInstance>
) => {
  return YBAxiosInstance<UniverseSoftwareUpgradeFinalizeRespResponse>(
    { url: `/customers/${cUUID}/universes/${uniUUID}/upgrade/software/finalize`, method: 'get' },
    options
  );
};

export const getGetFinalizeSoftwareUpgradeInfoQueryKey = (
  uniUUID: string,
  cUUID = localStorage.getItem('customerId')!
) => [`/customers/${cUUID}/universes/${uniUUID}/upgrade/software/finalize`];

export const useGetFinalizeSoftwareUpgradeInfo = <
  TData = AsyncReturnType<typeof getFinalizeSoftwareUpgradeInfo>,
  TError = ErrorType<void>
>(
  uniUUID: string,
  cUUID = localStorage.getItem('customerId')!,
  options?: {
    query?: UseQueryOptions<AsyncReturnType<typeof getFinalizeSoftwareUpgradeInfo>, TError, TData>;
    request?: SecondParameter<typeof YBAxiosInstance>;
  }
): UseQueryResult<TData, TError> & { queryKey: QueryKey } => {
  const { query: queryOptions, request: requestOptions } = options || {};

  const queryKey =
    queryOptions?.queryKey ?? getGetFinalizeSoftwareUpgradeInfoQueryKey(uniUUID, cUUID);

  const queryFn: QueryFunction<AsyncReturnType<typeof getFinalizeSoftwareUpgradeInfo>> = () =>
    getFinalizeSoftwareUpgradeInfo(uniUUID, cUUID, requestOptions);

  const query = useQuery<AsyncReturnType<typeof getFinalizeSoftwareUpgradeInfo>, TError, TData>(
    queryKey,
    queryFn,
    { enabled: !!(cUUID && uniUUID), ...queryOptions }
  );

  return {
    queryKey,
    ...query
  };
};

/**
 * Queues a task to perform a third party software upgrade.
 * @summary Upgrade third party software
 */
export const startThirdPartySoftwareUpgrade = (
  uniUUID: string,
  universeThirdPartySoftwareUpgradeReqBody: UniverseThirdPartySoftwareUpgradeReqBody,
  cUUID = localStorage.getItem('customerId')!,
  options?: SecondParameter<typeof YBAxiosInstance>
) => {
  return YBAxiosInstance<YBATaskRespResponse>(
    {
      url: `/customers/${cUUID}/universes/${uniUUID}/upgrade/third-party-software`,
      method: 'post',
      data: universeThirdPartySoftwareUpgradeReqBody
    },
    options
  );
};

export const useStartThirdPartySoftwareUpgrade = <
  TError = ErrorType<void>,
  TContext = unknown
>(options?: {
  mutation?: UseMutationOptions<
    AsyncReturnType<typeof startThirdPartySoftwareUpgrade>,
    TError,
    { uniUUID: string; data: UniverseThirdPartySoftwareUpgradeReqBody; cUUID?: string },
    TContext
  >;
  request?: SecondParameter<typeof YBAxiosInstance>;
}) => {
  const { mutation: mutationOptions, request: requestOptions } = options || {};

  const mutationFn: MutationFunction<
    AsyncReturnType<typeof startThirdPartySoftwareUpgrade>,
    { uniUUID: string; data: UniverseThirdPartySoftwareUpgradeReqBody; cUUID?: string }
  > = (props) => {
    const { uniUUID, data, cUUID } = props || {};

    return startThirdPartySoftwareUpgrade(uniUUID, data, cUUID, requestOptions);
  };

  return useMutation<
    AsyncReturnType<typeof startThirdPartySoftwareUpgrade>,
    TError,
    { uniUUID: string; data: UniverseThirdPartySoftwareUpgradeReqBody; cUUID?: string },
    TContext
  >(mutationFn, mutationOptions);
};
/**
 * Queues a task to rollback a YugabyteDB Software upgrade.
 * @summary Rollback YugabyteDB version
 */
export const rollbackSoftwareUpgrade = (
  uniUUID: string,
  universeRollbackUpgradeReqBody: UniverseRollbackUpgradeReqBody,
  cUUID = localStorage.getItem('customerId')!,
  options?: SecondParameter<typeof YBAxiosInstance>
) => {
  return YBAxiosInstance<YBATaskRespResponse>(
    {
      url: `/customers/${cUUID}/universes/${uniUUID}/upgrade/software/rollback`,
      method: 'post',
      data: universeRollbackUpgradeReqBody
    },
    options
  );
};

export const useRollbackSoftwareUpgrade = <TError = ErrorType<void>, TContext = unknown>(options?: {
  mutation?: UseMutationOptions<
    AsyncReturnType<typeof rollbackSoftwareUpgrade>,
    TError,
    { uniUUID: string; data: UniverseRollbackUpgradeReqBody; cUUID?: string },
    TContext
  >;
  request?: SecondParameter<typeof YBAxiosInstance>;
}) => {
  const { mutation: mutationOptions, request: requestOptions } = options || {};

  const mutationFn: MutationFunction<
    AsyncReturnType<typeof rollbackSoftwareUpgrade>,
    { uniUUID: string; data: UniverseRollbackUpgradeReqBody; cUUID?: string }
  > = (props) => {
    const { uniUUID, data, cUUID } = props || {};

    return rollbackSoftwareUpgrade(uniUUID, data, cUUID, requestOptions);
  };

  return useMutation<
    AsyncReturnType<typeof rollbackSoftwareUpgrade>,
    TError,
    { uniUUID: string; data: UniverseRollbackUpgradeReqBody; cUUID?: string },
    TContext
  >(mutationFn, mutationOptions);
};
/**
 * Queues a task to perform a precheck for a YugabyteDB Software upgrade.
 * @summary Precheck YugabyteDB version upgrade
 */
export const precheckSoftwareUpgrade = (
  uniUUID: string,
  universeSoftwareUpgradePrecheckReqBody: UniverseSoftwareUpgradePrecheckReqBody,
  cUUID = localStorage.getItem('customerId')!,
  options?: SecondParameter<typeof YBAxiosInstance>
) => {
  return YBAxiosInstance<UniverseSoftwareUpgradePrecheckResponseResponse>(
    {
      url: `/customers/${cUUID}/universes/${uniUUID}/upgrade/software/precheck`,
      method: 'post',
      data: universeSoftwareUpgradePrecheckReqBody
    },
    options
  );
};

export const usePrecheckSoftwareUpgrade = <TError = ErrorType<void>, TContext = unknown>(options?: {
  mutation?: UseMutationOptions<
    AsyncReturnType<typeof precheckSoftwareUpgrade>,
    TError,
    { uniUUID: string; data: UniverseSoftwareUpgradePrecheckReqBody; cUUID?: string },
    TContext
  >;
  request?: SecondParameter<typeof YBAxiosInstance>;
}) => {
  const { mutation: mutationOptions, request: requestOptions } = options || {};

  const mutationFn: MutationFunction<
    AsyncReturnType<typeof precheckSoftwareUpgrade>,
    { uniUUID: string; data: UniverseSoftwareUpgradePrecheckReqBody; cUUID?: string }
  > = (props) => {
    const { uniUUID, data, cUUID } = props || {};

    return precheckSoftwareUpgrade(uniUUID, data, cUUID, requestOptions);
  };

  return useMutation<
    AsyncReturnType<typeof precheckSoftwareUpgrade>,
    TError,
    { uniUUID: string; data: UniverseSoftwareUpgradePrecheckReqBody; cUUID?: string },
    TContext
  >(mutationFn, mutationOptions);
};
/**
 * Restart a YugabyteDB Universe.
 * @summary Restart a YugabyteDB Universe
 */
export const restartUniverse = (
  uniUUID: string,
  universeRestartReqBody: UniverseRestartReqBody,
  cUUID = localStorage.getItem('customerId')!,
  options?: SecondParameter<typeof YBAxiosInstance>
) => {
  return YBAxiosInstance<YBATaskRespResponse>(
    {
      url: `/customers/${cUUID}/universes/${uniUUID}/restart`,
      method: 'post',
      data: universeRestartReqBody
    },
    options
  );
};

export const useRestartUniverse = <TError = ErrorType<void>, TContext = unknown>(options?: {
  mutation?: UseMutationOptions<
    AsyncReturnType<typeof restartUniverse>,
    TError,
    { uniUUID: string; data: UniverseRestartReqBody; cUUID?: string },
    TContext
  >;
  request?: SecondParameter<typeof YBAxiosInstance>;
}) => {
  const { mutation: mutationOptions, request: requestOptions } = options || {};

  const mutationFn: MutationFunction<
    AsyncReturnType<typeof restartUniverse>,
    { uniUUID: string; data: UniverseRestartReqBody; cUUID?: string }
  > = (props) => {
    const { uniUUID, data, cUUID } = props || {};

    return restartUniverse(uniUUID, data, cUUID, requestOptions);
  };

  return useMutation<
    AsyncReturnType<typeof restartUniverse>,
    TError,
    { uniUUID: string; data: UniverseRestartReqBody; cUUID?: string },
    TContext
  >(mutationFn, mutationOptions);
};
