import { useCallback } from 'react';
import { useDeepCompareEffect, useFirstMountState, useMountedState } from 'react-use';
import { AxiosError } from 'axios';
import { MutationOptions, QueryClient, useMutation } from 'react-query';
import { api, instanceTypeQueryKey, providerQueryKey } from './api';
import {
  InstanceTypeMutation,
  YBProviderMutation
} from '../../components/configRedesign/providerRedesign/types';
import { InstanceType, YBBeanValidationError, YBPError, YBPSuccess, YBPTask } from './dtos';
import { handleServerError } from '../../utils/errorHandlingUtils';
import { getCreateProviderErrorMessage } from '../../components/configRedesign/providerRedesign/forms/utils';

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
};
export const useCreateProvider = (
  queryClient: QueryClient,
  mutationOptions?: MutationOptions<YBPTask, Error | AxiosError, UseCreateProviderParams>
) =>
  useMutation(
    ({ values, shouldValidate }: UseCreateProviderParams) =>
      api.createProvider(values, shouldValidate),
    {
      ...mutationOptions,
      onSuccess: (response, variables, context) => {
        mutationOptions?.onSuccess
          ? mutationOptions.onSuccess(response, variables, context)
          : queryClient.invalidateQueries(providerQueryKey.ALL);
      },
      onError: (error, variables, context) => {
        error;
        mutationOptions?.onError
          ? mutationOptions.onError(error, variables, context)
          : handleServerError<YBBeanValidationError | YBPError>(
              error,
              getCreateProviderErrorMessage
            );
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
        : handleServerError(error);
    }
  });

type CreateInstanceTypeParams = {
  providerUUID: string;
  instanceType: InstanceTypeMutation;
};
export const useUpdateInstanceType = (
  queryClient: QueryClient,
  mutationOptions?: MutationOptions<InstanceType, Error | AxiosError, CreateInstanceTypeParams>
) =>
  useMutation(
    ({ providerUUID, instanceType }: CreateInstanceTypeParams) =>
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
          : handleServerError(error);
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
          : handleServerError(error);
      }
    }
  );
// --------------------------------------------------------------------------------------
