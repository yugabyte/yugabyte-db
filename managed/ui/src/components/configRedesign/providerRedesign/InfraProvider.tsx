/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import { useState } from 'react';
import { MutateOptions, useQuery, useQueryClient } from 'react-query';
import { toast } from 'react-toastify';
import { AxiosError } from 'axios';
import { isFunction } from 'lodash';
import { useDispatch } from 'react-redux';

import { api, providerQueryKey } from '../../../redesign/helpers/api';
import { assertUnreachableCase } from '../../../utils/errorHandlingUtils';
import { YBErrorIndicator, YBLoading } from '../../common/indicators';
import {
  KubernetesProviderType,
  ProviderLabel,
  CloudVendorProviders,
  ProviderCode
} from './constants';
import { ProviderListView } from './ProviderListView';
import { fetchTaskUntilItCompletes } from '../../../actions/xClusterReplication';
import { ProviderCreateView } from './forms/ProviderCreateView';
import { useCreateProvider, UseCreateProviderParams } from '../../../redesign/helpers/hooks';
import { fetchCloudMetadata } from '../../../actions/cloud';

import { YBProviderMutation } from './types';
import { YBPBeanValidationError, YBPError, YBPTask } from '../../../redesign/helpers/dtos';

import styles from './InfraProvider.module.scss';

type InfraProviderProps =
  | {
      providerCode: typeof CloudVendorProviders[number] | typeof ProviderCode.ON_PREM;
    }
  | {
      providerCode: typeof ProviderCode.KUBERNETES;
      kubernetesProviderType: KubernetesProviderType;
    };

export type CreateInfraProvider = (
  values: YBProviderMutation,
  options?: {
    shouldValidate?: boolean;
    ignoreValidationErrors?: boolean;
    mutateOptions?: MutateOptions<
      YBPTask,
      Error | AxiosError<YBPBeanValidationError | YBPError>,
      UseCreateProviderParams
    >;
  }
) => Promise<YBPTask>;

export const ProviderDashboardView = {
  LIST: 'list',
  CREATE: 'create'
} as const;
export type ProviderDashboardView = typeof ProviderDashboardView[keyof typeof ProviderDashboardView];

const DEFAULT_VIEW = ProviderDashboardView.LIST;

export const InfraProvider = (props: InfraProviderProps) => {
  const { providerCode } = props;
  const [currentView, setCurrentView] = useState<ProviderDashboardView>(DEFAULT_VIEW);

  const dispatch = useDispatch();
  const queryClient = useQueryClient();
  const providerListQuery = useQuery(providerQueryKey.ALL, () => api.fetchProviderList());
  const createProviderMutation = useCreateProvider(queryClient, {
    onSuccess: (response) => {
      queryClient.invalidateQueries(providerQueryKey.ALL);
      dispatch(fetchCloudMetadata());

      fetchTaskUntilItCompletes(response.taskUUID, (error: boolean) => {
        if (error) {
          toast.error(
            <span className={styles.alertMsg}>
              <i className="fa fa-exclamation-circle" />
              <span>{`${ProviderLabel[providerCode]} provider creation failed.`}</span>
              <a href={`/tasks/${response.taskUUID}`} rel="noopener noreferrer" target="_blank">
                View Details
              </a>
            </span>
          );
        }
        queryClient.invalidateQueries(providerQueryKey.ALL);
        dispatch(fetchCloudMetadata());
      });
    }
  });

  if (providerListQuery.isLoading || providerListQuery.isIdle) {
    return <YBLoading />;
  }

  if (providerListQuery.isError) {
    return <YBErrorIndicator customErrorMessage="Error fetching provider list" />;
  }

  const createInfraProvider: CreateInfraProvider = async (values, options) => {
    const { shouldValidate = false, ignoreValidationErrors = false, mutateOptions } = options ?? {};
    return createProviderMutation.mutateAsync(
      {
        values: values,
        shouldValidate: shouldValidate,
        ignoreValidationErrors: ignoreValidationErrors
      },
      {
        ...mutateOptions,
        onSuccess: (response, variables, context) => {
          if (isFunction(mutateOptions?.onSuccess)) {
            mutateOptions?.onSuccess(response, variables, context);
          }
          setCurrentView(ProviderDashboardView.LIST);
        }
      }
    );
  };

  const handleOnBack = () => {
    setCurrentView(DEFAULT_VIEW);
  };

  switch (currentView) {
    case ProviderDashboardView.LIST:
      return providerCode === ProviderCode.KUBERNETES ? (
        <ProviderListView
          providerCode={providerCode}
          setCurrentView={setCurrentView}
          kubernetesProviderType={props.kubernetesProviderType}
        />
      ) : (
        <ProviderListView providerCode={providerCode} setCurrentView={setCurrentView} />
      );

    case ProviderDashboardView.CREATE:
      return providerCode === ProviderCode.KUBERNETES ? (
        <ProviderCreateView
          providerCode={providerCode}
          handleOnBack={handleOnBack}
          createInfraProvider={createInfraProvider}
          kubernetesProviderType={props.kubernetesProviderType}
        />
      ) : (
        <ProviderCreateView
          providerCode={providerCode}
          handleOnBack={handleOnBack}
          createInfraProvider={createInfraProvider}
        />
      );
    default:
      return assertUnreachableCase(currentView);
  }
};
