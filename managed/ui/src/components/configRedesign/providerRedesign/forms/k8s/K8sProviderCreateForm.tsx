/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import { useState } from 'react';
import JsYaml from 'js-yaml';
import { Box, CircularProgress, FormHelperText, Typography } from '@material-ui/core';
import { FormProvider, SubmitHandler, useForm } from 'react-hook-form';
import { array, mixed, object, string } from 'yup';
import { toast } from 'react-toastify';
import { yupResolver } from '@hookform/resolvers/yup';
import { useQuery } from 'react-query';
import { useSelector } from 'react-redux';

import { ACCEPTABLE_CHARS } from '../../../../config/constants';
import { KubernetesProvider, KubernetesProviderType, ProviderCode } from '../../constants';
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
import {
  KUBERNETES_PROVIDER_OPTIONS,
  QUAY_IMAGE_REGISTRY,
  REDHAT_IMAGE_REGISTRY
} from './constants';
import { RegionList } from '../../components/RegionList';
import { YBButton } from '../../../../common/forms/fields';
import { YBDropZoneField } from '../../components/YBDropZone/YBDropZoneField';
import { YBInputField } from '../../../../../redesign/components';
import { YBReactSelectField } from '../../components/YBReactSelect/YBReactSelectField';
import { addItem, deleteItem, editItem, getIsFormDisabled, readFileAsText } from '../utils';
import { YBLoading } from '../../../../common/indicators';
import { api, suggestedKubernetesConfigQueryKey } from '../../../../../redesign/helpers/api';
import { adaptSuggestedKubernetesConfig } from './utils';

import {
  K8sAvailabilityZoneMutation,
  K8sPullSecretFile,
  K8sRegionMutation,
  YBProviderMutation
} from '../../types';
import { RbacValidator } from '../../../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../../../redesign/features/rbac/ApiAndUserPermMapping';

interface K8sProviderCreateFormProps {
  createInfraProvider: CreateInfraProvider;
  kubernetesProviderType: KubernetesProviderType;
  onBack: () => void;
}

export interface K8sProviderCreateFormFieldValues {
  dbNodePublicInternetAccess: boolean;
  kubeConfigName: string;
  kubernetesImageRegistry: string;
  kubernetesProvider: { value: KubernetesProvider; label: string };
  providerName: string;
  regions: K8sRegionField[];

  kubeConfigContent?: File;
  kubernetesPullSecretContent?: File;
  kubernetesPullSecretName?: string;
}

const VALIDATION_SCHEMA = object().shape({
  providerName: string()
    .required('Provider Name is required.')
    .matches(
      ACCEPTABLE_CHARS,
      'Provider name cannot contain special characters other than "-", and "_".'
    ),
  kubernetesProvider: object().required('Kubernetes provider is required.'),
  kubeConfigContent: mixed(),
  kubernetesPullSecretContent: mixed(),
  kubernetesImageRegistry: string().required('Image registry is required.'),
  regions: array().min(1, 'Provider configurations must contain at least one region.')
});

const FORM_NAME = 'K8sProviderCreateForm';

export const K8sProviderCreateForm = ({
  createInfraProvider,
  kubernetesProviderType,
  onBack
}: K8sProviderCreateFormProps) => {
  const [isRegionFormModalOpen, setIsRegionFormModalOpen] = useState<boolean>(false);
  const [isDeleteRegionModalOpen, setIsDeleteRegionModalOpen] = useState<boolean>(false);
  const [regionSelection, setRegionSelection] = useState<K8sRegionField>();
  const [regionOperation, setRegionOperation] = useState<RegionOperation>(RegionOperation.ADD);
  const featureFlags = useSelector((state: any) => state.featureFlags);

  const defaultValues = constructDefaultFormValues(kubernetesProviderType);
  const formMethods = useForm<K8sProviderCreateFormFieldValues>({
    defaultValues: defaultValues,
    resolver: yupResolver(VALIDATION_SCHEMA)
  });

  const enableSuggestedConfigFeature =
    kubernetesProviderType === KubernetesProviderType.MANAGED_SERVICE &&
    !!(featureFlags.test.enablePrefillKubeConfig || featureFlags.released.enablePrefillKubeConfig);
  const suggestedKubernetesConfigQuery = useQuery(
    suggestedKubernetesConfigQueryKey.ALL,
    () => api.fetchSuggestedKubernetesConfig(),
    {
      enabled: enableSuggestedConfigFeature
    }
  );

  if (
    enableSuggestedConfigFeature &&
    (suggestedKubernetesConfigQuery.isLoading || suggestedKubernetesConfigQuery.isIdle)
  ) {
    return <YBLoading />;
  }

  const showAddRegionFormModal = () => {
    setRegionSelection(undefined);
    setRegionOperation(RegionOperation.ADD);
    setIsRegionFormModalOpen(true);
  };
  const showEditRegionFormModal = () => {
    setRegionOperation(RegionOperation.EDIT_NEW);
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
      const kubernetesPullSecretContent = formValues.kubernetesPullSecretContent
        ? (await readFileAsText(formValues.kubernetesPullSecretContent)) ?? ''
        : '';

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
              ...(formValues.kubeConfigContent && {
                kubeConfigContent: (await readFileAsText(formValues.kubeConfigContent)) ?? '',
                ...(formValues.kubeConfigContent.name && {
                  kubeConfigName: formValues.kubeConfigContent.name
                })
              }),
              kubernetesImageRegistry: formValues.kubernetesImageRegistry,
              kubernetesProvider: formValues.kubernetesProvider.value,
              ...(formValues.kubernetesPullSecretContent && {
                kubernetesPullSecretContent:
                  (await readFileAsText(formValues.kubernetesPullSecretContent)) ?? '',
                ...(formValues.kubernetesPullSecretContent.name && {
                  kubernetesPullSecretName: formValues.kubernetesPullSecretContent.name
                }),
                ...(kubernetesPullSecretYAML?.metadata?.name && {
                  kubernetesImagePullSecretName: kubernetesPullSecretYAML?.metadata?.name
                })
              })
            }
          }
        },
        regions: await Promise.all(
          formValues.regions.map<Promise<K8sRegionMutation>>(async (regionField) => {
            // Preprocess the zones data collected from the form.
            // Store the zones data in a format compatiable with expected API payload.
            const preprocessedZones = await Promise.all(
              regionField.zones.map<Promise<K8sAvailabilityZoneMutation>>(async (azFormValues) => ({
                code: azFormValues.code,
                name: azFormValues.code,
                details: {
                  cloudInfo: {
                    [ProviderCode.KUBERNETES]: {
                      ...(azFormValues.kubeConfigContent && {
                        kubeConfigContent:
                          (await readFileAsText(azFormValues.kubeConfigContent)) ?? '',
                        ...(azFormValues.kubeConfigContent.name && {
                          kubeConfigName: azFormValues.kubeConfigContent.name
                        })
                      }),
                      ...(azFormValues.kubeDomain && { kubeDomain: azFormValues.kubeDomain }),
                      ...(azFormValues.kubeNamespace && {
                        kubeNamespace: azFormValues.kubeNamespace
                      }),
                      ...(azFormValues.kubePodAddressTemplate && {
                        kubePodAddressTemplate: azFormValues.kubePodAddressTemplate
                      }),
                      ...(azFormValues.kubernetesStorageClass && {
                        kubernetesStorageClass: azFormValues.kubernetesStorageClass
                      }),
                      ...(azFormValues.overrides && { overrides: azFormValues.overrides }),
                      ...(azFormValues.certIssuerName && {
                        ...(azFormValues.certIssuerType === K8sCertIssuerType.CLUSTER_ISSUER && {
                          certManagerClusterIssuer: azFormValues.certIssuerName
                        }),
                        ...(azFormValues.certIssuerType === K8sCertIssuerType.ISSUER && {
                          certManagerIssuer: azFormValues.certIssuerName
                        })
                      })
                    }
                  }
                }
              }))
            );

            const newRegion: K8sRegionMutation = {
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
      await createInfraProvider(providerPayload);
    }
  };

  const suggestedKubernetesConfig = suggestedKubernetesConfigQuery.data;
  const applySuggestedConfig = () => {
    if (!suggestedKubernetesConfig) {
      return;
    }
    const {
      kubernetesImageRegistry,
      kubernetesProvider,
      kubernetesPullSecretContent,
      providerName,
      regions
    } = adaptSuggestedKubernetesConfig(suggestedKubernetesConfig);

    formMethods.setValue('kubernetesPullSecretContent', kubernetesPullSecretContent);
    formMethods.setValue('kubernetesImageRegistry', kubernetesImageRegistry);
    formMethods.setValue('kubernetesProvider', kubernetesProvider);
    formMethods.setValue('providerName', providerName);
    formMethods.setValue('regions', regions, { shouldValidate: true });
  };

  const regions = formMethods.watch('regions', defaultValues.regions);
  const setRegions = (regions: K8sRegionField[]) =>
    formMethods.setValue('regions', regions, { shouldValidate: true });
  const onRegionFormSubmit = (currentRegion: K8sRegionField) => {
    regionOperation === RegionOperation.ADD
      ? addItem(currentRegion, regions, setRegions)
      : editItem(currentRegion, regions, setRegions);
  };
  const onDeleteRegionSubmit = (currentRegion: K8sRegionField) =>
    deleteItem(currentRegion, regions, setRegions);

  const kubernetesProvider = formMethods.watch('kubernetesProvider');
  const isFormDisabled = getIsFormDisabled(formMethods.formState);
  return (
    <Box display="flex" justifyContent="center">
      <FormProvider {...formMethods}>
        <FormContainer name="K8sProviderForm" onSubmit={formMethods.handleSubmit(onFormSubmit)}>
          <Box display="flex">
            <Typography variant="h3">Create Kubernetes Provider Configuration</Typography>
            <Box marginLeft="auto">
              {enableSuggestedConfigFeature && (
                <YBButton
                  btnText="Autofill local cluster config"
                  btnClass="btn btn-default"
                  btnType="button"
                  onClick={() => applySuggestedConfig()}
                  disabled={isFormDisabled || !suggestedKubernetesConfig}
                  data-testid={`${FORM_NAME}-UseSuggestedConfigButton`}
                />
              )}
            </Box>
          </Box>
          <FormField providerNameField={true}>
            <FieldLabel>Provider Name</FieldLabel>
            <YBInputField
              control={formMethods.control}
              name="providerName"
              fullWidth
              disabled={isFormDisabled}
            />
          </FormField>
          <Box width="100%" display="flex" flexDirection="column" gridGap="32px">
            <FieldGroup heading="Cloud Info">
              <FormField>
                <FieldLabel>Kubernetes Provider Type</FieldLabel>
                <YBReactSelectField
                  control={formMethods.control}
                  name="kubernetesProvider"
                  options={KUBERNETES_PROVIDER_OPTIONS[kubernetesProviderType]}
                  isDisabled={isFormDisabled}
                />
              </FormField>
              <FormField>
                <FieldLabel>Image Registry</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="kubernetesImageRegistry"
                  placeholder={
                    kubernetesProviderType === KubernetesProviderType.OPEN_SHIFT
                      ? REDHAT_IMAGE_REGISTRY
                      : QUAY_IMAGE_REGISTRY
                  }
                  fullWidth
                  disabled={isFormDisabled}
                />
              </FormField>
              <FormField>
                <FieldLabel
                  infoTitle="Pull Secret"
                  infoContent="A pull secret file is required when pulling an image from a private container image registry or repository."
                >
                  Pull Secret
                </FieldLabel>
                <YBDropZoneField
                  name="kubernetesPullSecretContent"
                  control={formMethods.control}
                  actionButtonText="Upload Pull Secret File"
                  multipleFiles={false}
                  showHelpText={false}
                  disabled={isFormDisabled}
                />
              </FormField>
              <FormField>
                <FieldLabel>Kube Config (Optional)</FieldLabel>
                <YBDropZoneField
                  name="kubeConfigContent"
                  control={formMethods.control}
                  actionButtonText="Upload Kube Config File"
                  multipleFiles={false}
                  showHelpText={false}
                  disabled={isFormDisabled}
                />
              </FormField>
            </FieldGroup>
            <FieldGroup
              heading="Regions"
              headerAccessories={
                regions.length > 0 ? (
                  <RbacValidator accessRequiredOn={ApiPermissionMap.CREATE_PROVIDER} isControl>
                    <YBButton
                      btnIcon="fa fa-plus"
                      btnText="Add Region"
                      btnClass="btn btn-default"
                      btnType="button"
                      onClick={showAddRegionFormModal}
                      disabled={isFormDisabled}
                      data-testid={`${FORM_NAME}-AddRegionButton`}
                    />
                  </RbacValidator>
                ) : null
              }
            >
              <RegionList
                providerCode={ProviderCode.KUBERNETES}
                regions={regions}
                setRegionSelection={setRegionSelection}
                showAddRegionFormModal={showAddRegionFormModal}
                showEditRegionFormModal={showEditRegionFormModal}
                showDeleteRegionModal={showDeleteRegionModal}
                disabled={isFormDisabled}
                isError={!!formMethods.formState.errors.regions}
              />
              {formMethods.formState.errors.regions?.message && (
                <FormHelperText error={true}>
                  {formMethods.formState.errors.regions?.message}
                </FormHelperText>
              )}
            </FieldGroup>
            {(formMethods.formState.isValidating || formMethods.formState.isSubmitting) && (
              <Box display="flex" gridGap="5px" marginLeft="auto">
                <CircularProgress size={16} color="primary" thickness={5} />
              </Box>
            )}
          </Box>
          <Box marginTop="16px">
            <YBButton
              btnText="Create Provider Configuration"
              btnClass="btn btn-default save-btn"
              btnType="submit"
              disabled={isFormDisabled || formMethods.formState.isValidating}
              data-testid={`${FORM_NAME}-SubmitButton`}
            />
            <YBButton
              btnText="Back"
              btnClass="btn btn-default"
              onClick={onBack}
              disabled={isFormDisabled}
              data-testid={`${FORM_NAME}-BackButton`}
            />
          </Box>
        </FormContainer>
      </FormProvider>
      {isRegionFormModalOpen && (
        <ConfigureK8sRegionModal
          configuredRegions={regions}
          isProviderFormDisabled={isFormDisabled}
          kubernetesProvider={kubernetesProvider.value}
          onClose={hideRegionFormModal}
          onRegionSubmit={onRegionFormSubmit}
          open={isRegionFormModalOpen}
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

const constructDefaultFormValues = (
  kubernetesProviderType: KubernetesProviderType
): Partial<K8sProviderCreateFormFieldValues> => ({
  kubernetesProvider: KUBERNETES_PROVIDER_OPTIONS[kubernetesProviderType][0],
  regions: [] as K8sRegionField[]
});
