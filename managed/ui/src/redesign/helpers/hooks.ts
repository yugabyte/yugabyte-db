import { useCallback } from 'react';
import { useDeepCompareEffect, useFirstMountState, useMountedState } from 'react-use';
import { AxiosError } from 'axios';
import { MutationOptions, QueryClient, useMutation } from 'react-query';
import { toast } from 'react-toastify';

import { api, instanceTypeQueryKey, providerQueryKey, xClusterQueryKey } from './api';
import {
  InstanceTypeMutation,
  YBProviderMutation
} from '../../components/configRedesign/providerRedesign/types';
import { handleServerError } from '../../utils/errorHandlingUtils';
import {
  getCreateProviderErrorMessage,
  getEditProviderErrorMessage
} from '../../components/configRedesign/providerRedesign/forms/utils';
import { syncXClusterConfigWithDB } from '../../actions/xClusterReplication';

import { InstanceType, YBPSuccess, YBPTask } from './dtos';

// run callback when component is mounted only
export const useWhenMounted = () => {
  const isMounted = useMountedState();

  return useCallback(
    (callback: Function): void => {
      if (isMounted()) callback();
    },
    [isMounted]
  );
};

// same as original useEffect() but:
// - ignore the first forced invocation on mount, i.e. run effect on dependencies update only
// - compare deps by content instead of by reference
export const useDeepCompareUpdateEffect: typeof useDeepCompareEffect = (effect, deps) => {
  const isFirstRender = useFirstMountState();

  useDeepCompareEffect(() => {
    if (!isFirstRender) {
      return effect();
    }
  }, deps);
};

// --------------------------------------------------------------------------------------
// Resource Management Custom Hooks
// --------------------------------------------------------------------------------------

export type UseCreateProviderParams = {
  values: YBProviderMutation;
  shouldValidate: boolean;
  ignoreValidationErrors: boolean;
};
export const useCreateProvider = (
  queryClient: QueryClient,
  mutationOptions?: MutationOptions<YBPTask, Error | AxiosError, UseCreateProviderParams>
) =>
  useMutation(
    ({ values, shouldValidate, ignoreValidationErrors }: UseCreateProviderParams) =>
      api.createProvider(values, shouldValidate, ignoreValidationErrors),
    {
      ...mutationOptions,
      onSuccess: (response, variables, context) => {
        mutationOptions?.onSuccess
          ? mutationOptions.onSuccess(response, variables, context)
          : queryClient.invalidateQueries(providerQueryKey.ALL);
      },
      onError: (error, variables, context) => {
        mutationOptions?.onError
          ? mutationOptions.onError(error, variables, context)
          : handleServerError(error, { customErrorExtractor: getCreateProviderErrorMessage });
      }
    }
  );

export type UseEditProviderParams = {
  providerUUID: string;
  values: YBProviderMutation;
  shouldValidate: boolean;
  ignoreValidationErrors: boolean;
};
export const useEditProvider = (
  queryClient: QueryClient,
  mutationOptions?: MutationOptions<YBPTask, Error | AxiosError, UseEditProviderParams>
) =>
  useMutation(
    ({ providerUUID, values, shouldValidate, ignoreValidationErrors }: UseEditProviderParams) =>
      api.editProvider(providerUUID, values, shouldValidate, ignoreValidationErrors),
    {
      ...mutationOptions,
      onSuccess: (response, variables, context) => {
        if (mutationOptions?.onSuccess) {
          mutationOptions.onSuccess(response, variables, context);
        } else {
          queryClient.invalidateQueries(providerQueryKey.ALL, { exact: true });
          queryClient.invalidateQueries(providerQueryKey.detail(variables.providerUUID), {
            exact: true
          });
        }
      },
      onError: (error, variables, context) => {
        mutationOptions?.onError
          ? mutationOptions.onError(error, variables, context)
          : handleServerError(error, { customErrorExtractor: getEditProviderErrorMessage });
      }
    }
  );

type UseDeleteProviderParams = {
  providerUUID: string;
};
export const useDeleteProvider = (
  queryClient: QueryClient,
  mutationOptions?: MutationOptions<YBPTask, Error | AxiosError, UseDeleteProviderParams>
) =>
  useMutation(({ providerUUID }: UseDeleteProviderParams) => api.deleteProvider(providerUUID), {
    ...mutationOptions,
    onSuccess: (response, variables, context) => {
      if (mutationOptions?.onSuccess) {
        mutationOptions.onSuccess(response, variables, context);
      } else {
        queryClient.invalidateQueries(providerQueryKey.ALL, { exact: true });
        queryClient.invalidateQueries(providerQueryKey.detail(variables.providerUUID), {
          exact: true
        });
      }
    },
    onError: (error, variables, context) => {
      mutationOptions?.onError
        ? mutationOptions.onError(error, variables, context)
        : handleServerError(error, { customErrorLabel: 'Delete provider request failed' });
    }
  });

type UpdateInstanceTypeParams = {
  providerUUID: string;
  instanceType: InstanceTypeMutation;
};
export const useUpdateInstanceType = (
  queryClient: QueryClient,
  mutationOptions?: MutationOptions<InstanceType, Error | AxiosError, UpdateInstanceTypeParams>
) =>
  useMutation(
    ({ providerUUID, instanceType }: UpdateInstanceTypeParams) =>
      api.createInstanceType(providerUUID, instanceType),
    {
      ...mutationOptions,
      onSuccess: (response, variables, context) => {
        if (mutationOptions?.onSuccess) {
          mutationOptions.onSuccess(response, variables, context);
        } else {
          queryClient.invalidateQueries(instanceTypeQueryKey.ALL, { exact: true });
          queryClient.invalidateQueries(instanceTypeQueryKey.provider(variables.providerUUID), {
            exact: true
          });
        }
      },
      onError: (error, variables, context) => {
        mutationOptions?.onError
          ? mutationOptions.onError(error, variables, context)
          : handleServerError(error, { customErrorLabel: 'Update instance type request failed' });
      }
    }
  );

type DeleteInstanceTypeParams = {
  providerUUID: string;
  instanceTypeCode: string;
};
export const useDeleteInstanceType = (
  queryClient: QueryClient,
  mutationOptions?: MutationOptions<YBPSuccess, Error | AxiosError, DeleteInstanceTypeParams>
) =>
  useMutation(
    ({ providerUUID, instanceTypeCode }: DeleteInstanceTypeParams) =>
      api.deleteInstanceType(providerUUID, instanceTypeCode),
    {
      ...mutationOptions,
      onSuccess: (response, variables, context) => {
        if (mutationOptions?.onSuccess) {
          mutationOptions.onSuccess(response, variables, context);
        } else {
          queryClient.invalidateQueries(instanceTypeQueryKey.ALL, { exact: true });
          queryClient.invalidateQueries(instanceTypeQueryKey.provider(variables.providerUUID), {
            exact: true
          });
        }
      },
      onError: (error, variables, context) => {
        mutationOptions?.onError
          ? mutationOptions.onError(error, variables, context)
          : handleServerError(error, { customErrorLabel: 'Delete instance type request failed' });
      }
    }
  );

// --------------------------------------------------------------------------------------
