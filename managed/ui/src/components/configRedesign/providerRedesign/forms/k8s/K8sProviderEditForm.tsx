/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import React, { useState } from 'react';
import { FormProvider, SubmitHandler, useForm } from 'react-hook-form';
import { Box, FormHelperText, Typography } from '@material-ui/core';

import { FieldGroup } from '../components/FieldGroup';
import { FieldLabel } from '../components/FieldLabel';
import { FormContainer } from '../components/FormContainer';
import { FormField } from '../components/FormField';
import { K8sRegionField } from '../configureRegion/ConfigureK8sRegionModal';
import { KUBERNETES_PROVIDER_OPTIONS } from './constants';
import { KubernetesProviderLabel, NTPSetupType, ProviderCode } from '../../constants';
import { RegionList } from '../../components/RegionList';
import { YBButton } from '../../../../common/forms/fields';
import { YBInputField } from '../../../../../redesign/components';
import { YBReactSelectField } from '../../components/YBReactSelect/YBReactSelectField';
import { RegionOperation } from '../configureRegion/constants';

import { YBProvider } from '../../types';

interface K8sProviderEditFormProps {
  providerConfig: YBProvider;
}

export interface K8sProviderEditFormFieldValues {
  dbNodePublicInternetAccess: boolean;
  kubeConfig: string;
  kubeConfigContent: string;
  kubeConfigName: string;
  kubernetesImagePullSecretName: string;
  kubernetesImageRegistry: string;
  kubernetesProvider: { value: string; label: string };
  kubernetesPullSecret: string;
  kubernetesPullSecretContent: string;
  kubernetesPullSecretName: string;
  kubernetesServiceAccount: string;
  ntpServers: string[];
  ntpSetupType: NTPSetupType;
  providerName: string;
  regions: K8sRegionField[];
}

const READ_ONLY_KUBERNETES_PROVIDER_OPTIONS = {
  ...KUBERNETES_PROVIDER_OPTIONS.k8sManagedService,
  ...KUBERNETES_PROVIDER_OPTIONS.k8sDeprecated
} as const;
export const K8sProviderEditForm = ({ providerConfig }: K8sProviderEditFormProps) => {
  const [, setIsRegionFormModalOpen] = useState<boolean>(false);
  const [, setIsDeleteRegionModalOpen] = useState<boolean>(false);
  const [, setRegionSelection] = useState<K8sRegionField>();
  const [, setRegionOperation] = useState<RegionOperation>(RegionOperation.ADD);

  const defaultValues = {
    providerName: providerConfig.name,
    dbNodePublicInternetAccess: !providerConfig.details.airGapInstall,
    ...(providerConfig.code === ProviderCode.KUBERNETES && {
      regions: providerConfig.regions,
      kubeConfig: providerConfig.details.cloudInfo.kubernetes.kubeConfig,
      kubeConfigName: providerConfig.details.cloudInfo.kubernetes.kubeConfigName,
      kubernetesImagePullSecretName:
        providerConfig.details.cloudInfo.kubernetes.kubernetesImagePullSecretName,
      kubernetesImageRegistry: providerConfig.details.cloudInfo.kubernetes.kubernetesImageRegistry,
      kubernetesProvider: {
        value: providerConfig.details.cloudInfo.kubernetes.kubernetesProvider,
        label:
          KubernetesProviderLabel[providerConfig.details.cloudInfo.kubernetes.kubernetesProvider]
      },
      kubernetesPullSecret: providerConfig.details.cloudInfo.kubernetes.kubernetesPullSecret,
      kubernetesServiceAccount:
        providerConfig.details.cloudInfo.kubernetes.kubernetesServiceAccount,
      kubernetesStorageClass: providerConfig.details.cloudInfo.kubernetes.kubernetesStorageClass
    })
  };
  const formMethods = useForm<K8sProviderEditFormFieldValues>({
    defaultValues: defaultValues
  });

  const showAddRegionFormModal = () => {
    setRegionSelection(undefined);
    setRegionOperation(RegionOperation.ADD);
    setIsRegionFormModalOpen(true);
  };
  const showEditRegionFormModal = () => {
    setRegionOperation(RegionOperation.EDIT);
    setIsRegionFormModalOpen(true);
  };
  const showDeleteRegionModal = () => {
    setIsDeleteRegionModalOpen(true);
  };

  const onFormSubmit: SubmitHandler<K8sProviderEditFormFieldValues> = async (formValues) => {};

  const regions = formMethods.watch('regions');
  return (
    <Box display="flex" justifyContent="center">
      <FormProvider {...formMethods}>
        <FormContainer name="K8sProviderForm" onSubmit={formMethods.handleSubmit(onFormSubmit)}>
          <Typography variant="h3">Create Kubernetes Provider Configuration</Typography>
          <FormField providerNameField={true}>
            <FieldLabel>Provider Name</FieldLabel>
            <YBInputField
              control={formMethods.control}
              name="providerName"
              fullWidth
              disabled={true}
            />
          </FormField>
          <Box width="100%" display="flex" flexDirection="column" gridGap="32px">
            <FieldGroup heading="Cloud Info">
              <FormField>
                <FieldLabel>Kubernetes Provider Type</FieldLabel>
                <YBReactSelectField
                  control={formMethods.control}
                  name="kubernetesProvider"
                  options={READ_ONLY_KUBERNETES_PROVIDER_OPTIONS}
                  isDisabled={true}
                />
              </FormField>
              <FormField>
                <FieldLabel>Image Registry</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="kubernetesImageRegistry"
                  fullWidth
                  disabled={true}
                />
              </FormField>
              <FormField>
                <FieldLabel>Service Account Name</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="kubernetesServiceAccount"
                  fullWidth
                  disabled={true}
                />
              </FormField>
              <FormField>
                <FieldLabel>Image Pull Secret Name</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="kubernetesImagePullSecretName"
                  fullWidth
                  disabled={true}
                />
              </FormField>
              <FormField>
                {/* READONLY */}
                <FieldLabel>Kube Config Filepath</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="kubeConfig"
                  fullWidth
                  disabled={true}
                />
              </FormField>
              <FormField>
                {/* READONLY */}
                <FieldLabel>Pull Secret Filepath</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="kubernetesPullSecret"
                  fullWidth
                  disabled={true}
                />
              </FormField>
            </FieldGroup>
            <FieldGroup
              heading="Regions"
              headerAccessories={
                <YBButton
                  btnIcon="fa fa-plus"
                  btnText="Add Region"
                  btnClass="btn btn-default"
                  btnType="button"
                  onClick={showAddRegionFormModal}
                  disabled={formMethods.formState.isSubmitting}
                />
              }
            >
              <RegionList
                providerCode={ProviderCode.KUBERNETES}
                regions={regions}
                setRegionSelection={setRegionSelection}
                showAddRegionFormModal={showAddRegionFormModal}
                showEditRegionFormModal={showEditRegionFormModal}
                showDeleteRegionModal={showDeleteRegionModal}
                disabled={formMethods.formState.isSubmitting}
                isError={!!formMethods.formState.errors.regions}
              />
              {formMethods.formState.errors.regions?.message && (
                <FormHelperText error={true}>
                  {formMethods.formState.errors.regions?.message}
                </FormHelperText>
              )}
            </FieldGroup>
          </Box>
        </FormContainer>
      </FormProvider>
    </Box>
  );
};
