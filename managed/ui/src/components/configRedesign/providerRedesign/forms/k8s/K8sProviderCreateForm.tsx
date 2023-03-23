/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import React, { useState } from 'react';
import JsYaml from 'js-yaml';
import { Box, FormHelperText, Typography } from '@material-ui/core';
import { FormProvider, SubmitHandler, useForm } from 'react-hook-form';
import { array, mixed, object, string } from 'yup';
import { toast } from 'react-toastify';
import { yupResolver } from '@hookform/resolvers/yup';

import { ACCEPTABLE_CHARS } from '../../../../config/constants';
import { ASYNC_ERROR, KubernetesProviderType, ProviderCode } from '../../constants';
import { CreateInfraProvider } from '../../InfraProvider';
import { DeleteRegionModal } from '../../components/DeleteRegionModal';
import { FieldGroup } from '../components/FieldGroup';
import { FieldLabel } from '../components/FieldLabel';
import { FormContainer } from '../components/FormContainer';
import { FormField } from '../components/FormField';
import { K8sCertIssuerType, RegionOperation } from '../configureRegion/constants';
import {
  K8sRegionField,
  ConfigureK8sRegionModal
} from '../configureRegion/ConfigureK8sRegionModal';
import { KUBERNETES_PROVIDER_OPTIONS } from './constants';
import { RegionList } from '../../components/RegionList';
import { YBButton } from '../../../../common/forms/fields';
import { YBDropZoneField } from '../../components/YBDropZone/YBDropZoneField';
import { YBInputField } from '../../../../../redesign/components';
import { YBReactSelectField } from '../../components/YBReactSelect/YBReactSelectField';
import { addItem, deleteItem, editItem, handleFormServerError, readFileAsText } from '../utils';

import {
  K8sAvailabilityZoneMutation,
  K8sPullSecretFile,
  K8sRegionMutation,
  YBProviderMutation
} from '../../types';

interface K8sProviderCreateFormProps {
  createInfraProvider: CreateInfraProvider;
  kubernetesProviderType: KubernetesProviderType;
  onBack: () => void;
}

export interface K8sProviderCreateFormFieldValues {
  dbNodePublicInternetAccess: boolean;
  kubeConfigContent: File;
  kubeConfigName: string;
  kubernetesImageRegistry: string;
  kubernetesProvider: { value: string; label: string };
  kubernetesPullSecretContent: File;
  kubernetesPullSecretName: string;
  kubernetesServiceAccount: string;
  providerName: string;
  regions: K8sRegionField[];

  [ASYNC_ERROR]: string;
}

export const DEFAULT_FORM_VALUES: Partial<K8sProviderCreateFormFieldValues> = {
  regions: [] as K8sRegionField[]
} as const;

const VALIDATION_SCHEMA = object().shape({
  providerName: string()
    .required('Provider Name is required.')
    .matches(
      ACCEPTABLE_CHARS,
      'Provider name cannot contain special characters other than "-", and "_".'
    ),
  kubernetesProvider: object().required('Kubernetes provider is required.'),
  kubeConfigContent: mixed().required('Kube Config file is required.'),
  kubernetesPullSecretContent: mixed().required('Kubernetes pull secret file is required.'),
  kubernetesServiceAccount: string().required('Service account name is required.'),
  kubernetesImageRegistry: string().required('Image registry is required.'),
  regions: array().min(1, 'Provider configurations must contain at least one region.')
});

export const K8sProviderCreateForm = ({
  createInfraProvider,
  kubernetesProviderType,
  onBack
}: K8sProviderCreateFormProps) => {
  const [isRegionFormModalOpen, setIsRegionFormModalOpen] = useState<boolean>(false);
  const [isDeleteRegionModalOpen, setIsDeleteRegionModalOpen] = useState<boolean>(false);
  const [regionSelection, setRegionSelection] = useState<K8sRegionField>();
  const [regionOperation, setRegionOperation] = useState<RegionOperation>(RegionOperation.ADD);
  const formMethods = useForm<K8sProviderCreateFormFieldValues>({
    defaultValues: DEFAULT_FORM_VALUES,
    resolver: yupResolver(VALIDATION_SCHEMA)
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
  const hideDeleteRegionModal = () => {
    setIsDeleteRegionModalOpen(false);
  };
  const hideRegionFormModal = () => {
    setIsRegionFormModalOpen(false);
  };

  const onFormSubmit: SubmitHandler<K8sProviderCreateFormFieldValues> = async (formValues) => {
    let providerPayload: YBProviderMutation | null = null;
    try {
      const kubernetesPullSecretContent =
        (await readFileAsText(formValues.kubernetesPullSecretContent)) ?? '';

      // Type cast is required since JsYaml.load doesn't know the type of the input file
      const kubernetesPullSecretYAML = JsYaml.load(
        kubernetesPullSecretContent
      ) as K8sPullSecretFile;
      providerPayload = {
        code: ProviderCode.KUBERNETES,
        name: formValues.providerName,
        details: {
          airGapInstall: !formValues.dbNodePublicInternetAccess,
          cloudInfo: {
            [ProviderCode.KUBERNETES]: {
              kubeConfigContent: (await readFileAsText(formValues.kubeConfigContent)) ?? '',
              kubeConfigName: formValues.kubeConfigContent.name ?? '',
              kubernetesImageRegistry: formValues.kubernetesImageRegistry,
              kubernetesImagePullSecretName: kubernetesPullSecretYAML?.metadata.name,
              kubernetesProvider: formValues.kubernetesProvider.value,
              kubernetesPullSecretContent:
                (await readFileAsText(formValues.kubernetesPullSecretContent)) ?? '',
              kubernetesPullSecretName: formValues.kubernetesPullSecretContent.name ?? '',
              kubernetesServiceAccount: formValues.kubernetesServiceAccount
            }
          }
        },
        regions: await Promise.all(
          formValues.regions.map<Promise<K8sRegionMutation>>(async (regionField) => {
            // Preprocess the zones data collected from the form.
            // Store the zones data in a format compatiable with expected API payload.
            const preprocessedZones = await Promise.all(
              regionField.zones.map<Promise<K8sAvailabilityZoneMutation>>(async (zone) => ({
                code: zone.code,
                name: zone.code,
                details: {
                  cloudInfo: {
                    [ProviderCode.KUBERNETES]: {
                      ...(zone.kubeConfigContent && {
                        kubeConfigContent: (await readFileAsText(zone.kubeConfigContent)) ?? ''
                      }),
                      kubeDomain: zone.kubeDomain,
                      kubeNamespace: zone.kubeNamespace,
                      kubePodAddressTemplate: zone.kubePodAddressTemplate,
                      kubernetesStorageClasses: zone.kubernetesStorageClasses,
                      ...(zone.certIssuerType === K8sCertIssuerType.CLUSTER_ISSUER && {
                        certManagerClusterIssuer: zone.certIssuerName
                      }),
                      ...(zone.certIssuerType === K8sCertIssuerType.ISSUER && {
                        certManagerIssuer: zone.certIssuerName
                      })
                    }
                  }
                }
              }))
            );

            const newRegion = {
              code: regionField.regionData.value.code,
              name: regionField.regionData.label,
              zones: preprocessedZones,
              details: {
                cloudInfo: { [ProviderCode.KUBERNETES]: {} }
              }
            };
            return newRegion;
          })
        )
      };
    } catch {
      toast.error('An error occured while reading the form input files.');
    }
    if (providerPayload) {
      await createInfraProvider(providerPayload, {
        mutateOptions: {
          onError: (error) => handleFormServerError(error, ASYNC_ERROR, formMethods.setError)
        }
      });
    }
  };

  const regions = formMethods.watch('regions', DEFAULT_FORM_VALUES.regions);
  const setRegions = (regions: K8sRegionField[]) => formMethods.setValue('regions', regions);
  const onRegionFormSubmit = (currentRegion: K8sRegionField) => {
    regionOperation === RegionOperation.ADD
      ? addItem(currentRegion, regions, setRegions)
      : editItem(currentRegion, regions, setRegions);
  };
  const onDeleteRegionSubmit = (currentRegion: K8sRegionField) =>
    deleteItem(currentRegion, regions, setRegions);

  return (
    <Box display="flex" justifyContent="center">
      <FormProvider {...formMethods}>
        <FormContainer name="K8sProviderForm" onSubmit={formMethods.handleSubmit(onFormSubmit)}>
          <Typography variant="h3">Create Kubernetes Provider Configuration</Typography>
          <FormField providerNameField={true}>
            <FieldLabel>Provider Name</FieldLabel>
            <YBInputField control={formMethods.control} name="providerName" fullWidth />
          </FormField>
          <Box width="100%" display="flex" flexDirection="column" gridGap="32px">
            <FieldGroup heading="Cloud Info">
              <FormField>
                <FieldLabel>Kubernetes Provider Type</FieldLabel>
                <YBReactSelectField
                  control={formMethods.control}
                  name="kubernetesProvider"
                  options={KUBERNETES_PROVIDER_OPTIONS[kubernetesProviderType]}
                />
              </FormField>
              <FormField>
                <FieldLabel>Service Account Name</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="kubernetesServiceAccount"
                  fullWidth
                />
              </FormField>
              <FormField>
                <FieldLabel>Image Registry</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="kubernetesImageRegistry"
                  fullWidth
                />
              </FormField>
              <FormField>
                <FieldLabel>Kube Config</FieldLabel>
                <YBDropZoneField
                  name="kubeConfigContent"
                  control={formMethods.control}
                  actionButtonText="Upload Kube Config File"
                  multipleFiles={false}
                  showHelpText={false}
                />
              </FormField>
              <FormField>
                <FieldLabel>Pull Secret</FieldLabel>
                <YBDropZoneField
                  name="kubernetesPullSecretContent"
                  control={formMethods.control}
                  actionButtonText="Upload Pull Secret File"
                  multipleFiles={false}
                  showHelpText={false}
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
          <Box marginTop="16px">
            <YBButton
              btnText="Create Provider Configuration"
              btnClass="btn btn-default save-btn"
              btnType="submit"
              loading={formMethods.formState.isSubmitting}
              disabled={formMethods.formState.isSubmitting}
              data-testid="K8sProviderCreateForm-SubmitButton"
            />
            <YBButton
              btnText="Back"
              btnClass="btn btn-default"
              onClick={onBack}
              disabled={formMethods.formState.isSubmitting}
              data-testid="K8sProviderCreateForm-BackButton"
            />
          </Box>
        </FormContainer>
      </FormProvider>
      {isRegionFormModalOpen && (
        <ConfigureK8sRegionModal
          configuredRegions={regions}
          onClose={hideRegionFormModal}
          onRegionSubmit={onRegionFormSubmit}
          open={isRegionFormModalOpen}
          providerCode={ProviderCode.KUBERNETES}
          regionOperation={regionOperation}
          regionSelection={regionSelection}
        />
      )}
      <DeleteRegionModal
        region={regionSelection}
        onClose={hideDeleteRegionModal}
        open={isDeleteRegionModalOpen}
        deleteRegion={onDeleteRegionSubmit}
      />
    </Box>
  );
};
