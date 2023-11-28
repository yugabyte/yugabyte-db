/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import React from 'react';
import { AxiosError } from 'axios';
import { MutateOptions, useQueryClient } from 'react-query';
import { toast } from 'react-toastify';
import { makeStyles } from '@material-ui/core';

import { AWSProviderEditForm } from './aws/AWSProviderEditForm';
import { AZUProviderEditForm } from './azu/AZUProviderEditForm';
import { GCPProviderEditForm } from './gcp/GCPProviderEditForm';
import { K8sProviderEditForm } from './k8s/K8sProviderEditForm';
import { OnPremProviderEditForm } from './onPrem/OnPremProviderEditForm';
import { ProviderCode } from '../constants';
import { UseCreateProviderParams, useEditProvider } from '../../../../redesign/helpers/hooks';
import { YBPBeanValidationError, YBPError, YBPTask } from '../../../../redesign/helpers/dtos';
import { assertUnreachableCase } from '../../../../utils/errorHandlingUtils';
import { providerQueryKey } from '../../../../redesign/helpers/api';
import { fetchTaskUntilItCompletes } from '../../../../actions/xClusterReplication';
import { UniverseItem } from '../providerView/providerDetails/UniverseTable';

import { YBProvider, YBProviderMutation } from '../types';

interface ProviderEditViewProps {
  linkedUniverses: UniverseItem[];
  providerConfig: YBProvider;
}

/**
 * options.mutateOptions - These callbacks will be run only while the component is mounted.
 */
export type EditProvider = (
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

const useStyles = makeStyles((theme) => ({
  toastContainer: {
    display: 'flex',
    gap: theme.spacing(0.5),
    '& a': {
      textDecoration: 'underline',
      color: '#fff'
    }
  }
}));

export const ProviderEditView = ({ linkedUniverses, providerConfig }: ProviderEditViewProps) => {
  const queryClient = useQueryClient();
  const classes = useStyles();

  const editProviderMutation = useEditProvider(queryClient, {
    onSuccess: (data, variables) => {
      queryClient.invalidateQueries(providerQueryKey.ALL, { exact: true });
      queryClient.invalidateQueries(providerQueryKey.detail(variables.providerUUID));

      // Currently the edit provider request may perform some synchronous and some asynchronous
      // tasks depending on the properties being edited.
      // As of now, only `add new region` is asynchronous.
      // There is work underway to make the edit provider endpoint always return a taskUUID.
      // Until that is landed, we should do a check for non-falsy before fetching the task.
      if (data.taskUUID) {
        fetchTaskUntilItCompletes(data.taskUUID, (error: boolean) => {
          if (error) {
            toast.error(
              <span className={classes.toastContainer}>
                <i className="fa fa-exclamation-circle" />
                <span>Provider edit failed.</span>
                <a href={`/tasks/${data.taskUUID}`} rel="noopener noreferrer" target="_blank">
                  View Details
                </a>
              </span>
            );
          } else {
            toast.success('Provider edit succeeded.');
          }
          queryClient.invalidateQueries(providerQueryKey.ALL, { exact: true });
          queryClient.invalidateQueries(providerQueryKey.detail(variables.providerUUID));
        });
      } else {
        toast.success('Provider edit succeeded.');
      }
    }
  });
  const editProvider: EditProvider = async (values, options) => {
    const { shouldValidate = false, ignoreValidationErrors = false, mutateOptions } = options ?? {};
    return editProviderMutation.mutateAsync(
      {
        providerUUID: providerConfig.uuid,
        values: values,
        shouldValidate: shouldValidate,
        ignoreValidationErrors: ignoreValidationErrors
      },
      {
        ...mutateOptions
      }
    );
  };
  switch (providerConfig.code) {
    case ProviderCode.AWS:
      return (
        <AWSProviderEditForm
          providerConfig={providerConfig}
          editProvider={editProvider}
          linkedUniverses={linkedUniverses}
        />
      );
    case ProviderCode.GCP:
      return (
        <GCPProviderEditForm
          providerConfig={providerConfig}
          editProvider={editProvider}
          linkedUniverses={linkedUniverses}
        />
      );
    case ProviderCode.AZU:
      return (
        <AZUProviderEditForm
          providerConfig={providerConfig}
          editProvider={editProvider}
          linkedUniverses={linkedUniverses}
        />
      );
    case ProviderCode.KUBERNETES:
      return (
        <K8sProviderEditForm
          providerConfig={providerConfig}
          editProvider={editProvider}
          linkedUniverses={linkedUniverses}
        />
      );
    case ProviderCode.ON_PREM:
      return (
        <OnPremProviderEditForm
          providerConfig={providerConfig}
          editProvider={editProvider}
          linkedUniverses={linkedUniverses}
        />
      );
    default: {
      return assertUnreachableCase(providerConfig);
    }
  }
};
