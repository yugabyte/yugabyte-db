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
import { useTranslation } from 'react-i18next';

import { ACCEPTABLE_CHARS } from '../../../../config/constants';
import {
  KubernetesProvider,
  KubernetesProviderLabel,
  KubernetesProviderType,
  ProviderCode,
  ProviderOperation
} from '../../constants';
import { DeleteRegionModal } from '../../components/DeleteRegionModal';
import { FieldGroup } from '../components/FieldGroup';
import { FieldLabel } from '../components/FieldLabel';
import { FormContainer } from '../components/FormContainer';
import { FormField } from '../components/FormField';
import { SubmitInProgress } from '../components/SubmitInProgress';
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
import { YBInput, YBInputField, YBToggleField } from '../../../../../redesign/components';
import { YBReactSelectField } from '../../components/YBReactSelect/YBReactSelectField';
import {
  addItem,
  deleteItem,
  editItem,
  generateLowerCaseAlphanumericId,
  getIsFieldDisabled,
  getIsFormDisabled,
  readFileAsText,
  handleFormSubmitServerError,
  UseProviderValidationEnabled
} from '../utils';
import { EditProvider } from '../ProviderEditView';
import {
  findExistingRegion,
  findExistingZone,
  getCertIssuerType,
  getDeletedRegions,
  getDeletedZones,
  getInUseAzs,
  getKubernetesProviderType
} from '../../utils';
import { VersionWarningBanner } from '../components/VersionWarningBanner';
import {
  api,
  runtimeConfigQueryKey,
  suggestedKubernetesConfigQueryKey
} from '../../../../../redesign/helpers/api';
import { YBErrorIndicator, YBLoading } from '../../../../common/indicators';
import { adaptSuggestedKubernetesConfig } from './utils';
import { UniverseItem } from '../../providerView/providerDetails/UniverseTable';
import { CloudType } from '../../../../../redesign/helpers/dtos';
import {
  K8sAvailabilityZone,
  K8sAvailabilityZoneMutation,
  K8sProvider,
  K8sPullSecretFile,
  K8sRegion,
  K8sRegionMutation,
  YBProviderMutation
} from '../../types';
import {
  hasNecessaryPerm,
  RbacValidator
} from '../../../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../../../redesign/features/rbac/ApiAndUserPermMapping';
import { RuntimeConfigKey } from '../../../../../redesign/helpers/constants';
import { K8S_FORM_MAPPERS } from './constants';

interface K8sProviderEditFormProps {
  editProvider: EditProvider;
  linkedUniverses: UniverseItem[];
  providerConfig: K8sProvider;
}

export interface K8sProviderEditFormFieldValues {
  dbNodePublicInternetAccess: boolean;
  editKubeConfigContent: boolean;
  editPullSecretContent: boolean;
  kubeConfigName: string;
  kubernetesImageRegistry: string;
  kubernetesProvider: { value: KubernetesProvider; label: string };
  providerName: string;
  regions: K8sRegionField[];
  version: number;

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

const FORM_NAME = 'K8sProviderEditForm';

export const K8sProviderEditForm = ({
  editProvider,
  linkedUniverses,
  providerConfig
}: K8sProviderEditFormProps) => {
  const [isRegionFormModalOpen, setIsRegionFormModalOpen] = useState<boolean>(false);
  const [isDeleteRegionModalOpen, setIsDeleteRegionModalOpen] = useState<boolean>(false);
  const [regionSelection, setRegionSelection] = useState<K8sRegionField>();
  const [regionOperation, setRegionOperation] = useState<RegionOperation>(RegionOperation.ADD);
  const [isValidationErrorExist, setValidationErrorExist] = useState(false);
  const featureFlags = useSelector((state: any) => state.featureFlags);
  const { t } = useTranslation();

  const defaultValues = constructDefaultFormValues(providerConfig);
  const formMethods = useForm<K8sProviderEditFormFieldValues>({
    defaultValues: defaultValues,
    resolver: yupResolver(VALIDATION_SCHEMA)
  });
  const kubernetesProviderType = getKubernetesProviderType(
    providerConfig.details.cloudInfo.kubernetes.kubernetesProvider
  );
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

  const customerUUID = localStorage.getItem('customerId') ?? '';
  const customerRuntimeConfigQuery = useQuery(
    runtimeConfigQueryKey.customerScope(customerUUID),
    () => api.fetchRuntimeConfigs(customerUUID, true)
  );
  const {
    isLoading: isProviderValidationLoading,
    isValidationEnabled
  } = UseProviderValidationEnabled(CloudType.kubernetes);
  if (customerRuntimeConfigQuery.isError) {
    return (
      <YBErrorIndicator
        customErrorMessage={t('failedToFetchCustomerRuntimeConfig', { keyPrefix: 'queryError' })}
      />
    );
  }
  if (
    (enableSuggestedConfigFeature &&
      (suggestedKubernetesConfigQuery.isLoading || suggestedKubernetesConfigQuery.isIdle)) ||
    customerRuntimeConfigQuery.isLoading ||
    customerRuntimeConfigQuery.isIdle ||
    isProviderValidationLoading
  ) {
    return <YBLoading />;
  }

  const onFormSubmit = async (
    formValues: K8sProviderEditFormFieldValues,
    shouldValidate: boolean,
    ignoreValidationErrors = false
  ) => {
    try {
      setValidationErrorExist(false);
      const providerPayload = await constructProviderPayload(formValues, providerConfig);
      try {
        await editProvider(providerPayload, {
          shouldValidate: shouldValidate,
          ignoreValidationErrors: ignoreValidationErrors,
          mutateOptions: {
            onError: (err) => {
              handleFormSubmitServerError(
                (err as any)?.response?.data,
                formMethods,
                K8S_FORM_MAPPERS
              );
              setValidationErrorExist(true);
            }
          }
        });
      } catch (_) {
        // Handled with `mutateOptions.onError`
      }
    } catch (error: any) {
      toast.error(error.message ?? error);
    }
  };

  const onFormValidateAndSubmit: SubmitHandler<K8sProviderEditFormFieldValues> = async (
    formValues
  ) => await onFormSubmit(formValues, isValidationEnabled);
  const onFormForceSubmit: SubmitHandler<K8sProviderEditFormFieldValues> = async (formValues) =>
    await onFormSubmit(formValues, isValidationEnabled, true);

  const skipValidationAndSubmit = () => {
    onFormForceSubmit(formMethods.getValues());
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

    formMethods.setValue('editPullSecretContent', true);
    formMethods.setValue('kubernetesPullSecretContent', kubernetesPullSecretContent);
    formMethods.setValue('kubernetesImageRegistry', kubernetesImageRegistry);
    formMethods.setValue('kubernetesProvider', kubernetesProvider);
    formMethods.setValue('providerName', providerName);
    formMethods.setValue('regions', regions, { shouldValidate: true });
  };
  const onFormReset = () => {
    formMethods.reset(defaultValues);
  };
  const showAddRegionFormModal = () => {
    setRegionSelection(undefined);
    setRegionOperation(RegionOperation.ADD);
    setIsRegionFormModalOpen(true);
  };
  const showEditRegionFormModal = (regionOperation: RegionOperation) => {
    setRegionOperation(regionOperation);
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

  const currentProviderVersion = formMethods.watch('version', defaultValues.version);
  const editKubeConfigContent = formMethods.watch(
    'editKubeConfigContent',
    defaultValues.editKubeConfigContent
  );
  const editPullSecretContent = formMethods.watch(
    'editPullSecretContent',
    defaultValues.editPullSecretContent
  );
  const kubernetesProvider = formMethods.watch(
    'kubernetesProvider',
    defaultValues.kubernetesProvider
  );
  const kubernetesProviderTypeOptions = [
    ...(KUBERNETES_PROVIDER_OPTIONS[kubernetesProviderType] ?? []),
    ...KUBERNETES_PROVIDER_OPTIONS.k8sDeprecated
  ] as const;
  const existingRegions = providerConfig.regions.map((region) => region.code);
  const runtimeConfigEntries = customerRuntimeConfigQuery.data.configEntries ?? [];
  /**
   * In use zones for selected region.
   */
  const inUseZones = getInUseAzs(providerConfig.uuid, linkedUniverses, regionSelection?.code);
  const isEditInUseProviderEnabled = runtimeConfigEntries.some(
    (config: any) =>
      config.key === RuntimeConfigKey.EDIT_IN_USE_PORIVDER_UI_FEATURE_FLAG &&
      config.value === 'true'
  );
  const isProviderInUse = linkedUniverses.length > 0;
  const isFormDisabled =
    (!isEditInUseProviderEnabled && isProviderInUse) ||
    getIsFormDisabled(formMethods.formState, providerConfig) ||
    !hasNecessaryPerm(ApiPermissionMap.MODIFY_PROVIDER);
  return (
    <Box display="flex" justifyContent="center">
      <FormProvider {...formMethods}>
        <FormContainer
          name="K8sProviderForm"
          onSubmit={formMethods.handleSubmit(onFormValidateAndSubmit)}
        >
          {currentProviderVersion < providerConfig.version && (
            <VersionWarningBanner onReset={onFormReset} />
          )}
          <Box display="flex">
            <Typography variant="h3">Manage Kubernetes Provider Configuration</Typography>
            <Box marginLeft="auto">
              {enableSuggestedConfigFeature && (
                <YBButton
                  btnText="Autofill local cluster config"
                  btnClass="btn btn-default"
                  btnType="button"
                  onClick={() => applySuggestedConfig()}
                  disabled={isFormDisabled || !suggestedKubernetesConfig || isProviderInUse}
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
              disabled={getIsFieldDisabled(
                ProviderCode.KUBERNETES,
                'providerName',
                isFormDisabled,
                isProviderInUse
              )}
            />
          </FormField>
          <Box width="100%" display="flex" flexDirection="column" gridGap="32px">
            <FieldGroup heading="Cloud Info">
              <FormField>
                <FieldLabel>Kubernetes Provider Type</FieldLabel>
                <YBReactSelectField
                  control={formMethods.control}
                  name="kubernetesProvider"
                  options={kubernetesProviderTypeOptions}
                  defaultValue={defaultValues.kubernetesProvider}
                  isDisabled={getIsFieldDisabled(
                    ProviderCode.KUBERNETES,
                    'kubernetesProvider',
                    isFormDisabled,
                    isProviderInUse
                  )}
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
                  disabled={getIsFieldDisabled(
                    ProviderCode.KUBERNETES,
                    'kubernetesImageRegistry',
                    isFormDisabled,
                    isProviderInUse
                  )}
                />
              </FormField>
              <FormField>
                <FieldLabel>Current Pull Secret Filepath</FieldLabel>
                <YBInput
                  value={providerConfig.details.cloudInfo.kubernetes.kubernetesPullSecret ?? ''}
                  disabled={true}
                  fullWidth
                />
              </FormField>
              <FormField>
                <FieldLabel
                  infoTitle="Replace Pull Secret File"
                  infoContent="If no new pull secret file is uploaded, the existing pull secret is simply removed from the provider."
                >
                  Replace Pull Secret File
                </FieldLabel>
                <YBToggleField
                  name="editPullSecretContent"
                  control={formMethods.control}
                  disabled={getIsFieldDisabled(
                    ProviderCode.KUBERNETES,
                    'editPullSecretContent',
                    isFormDisabled,
                    isProviderInUse
                  )}
                />
              </FormField>
              {editPullSecretContent && (
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
                    disabled={getIsFieldDisabled(
                      ProviderCode.KUBERNETES,
                      'kubernetesPullSecretContent',
                      isFormDisabled,
                      isProviderInUse
                    )}
                  />
                </FormField>
              )}
              <FormField>
                <FieldLabel>Current Kube Config Filepath</FieldLabel>
                <YBInput
                  value={providerConfig.details.cloudInfo.kubernetes.kubeConfig ?? ''}
                  disabled={true}
                  fullWidth
                />
              </FormField>
              <FormField>
                <FieldLabel
                  infoTitle="Replace Kube Config File"
                  infoContent="If no new Kube config file is uploaded, the existing Kube config file is simply removed from the provider."
                >
                  Replace Kube Config File
                </FieldLabel>
                <YBToggleField
                  name="editKubeConfigContent"
                  control={formMethods.control}
                  disabled={getIsFieldDisabled(
                    ProviderCode.KUBERNETES,
                    'editKubeConfigContent',
                    isFormDisabled,
                    isProviderInUse
                  )}
                />
              </FormField>
              {editKubeConfigContent && (
                <FormField>
                  <FieldLabel>Kube Config (Optional)</FieldLabel>
                  <YBDropZoneField
                    name="kubeConfigContent"
                    control={formMethods.control}
                    actionButtonText="Upload Kube Config File"
                    multipleFiles={false}
                    showHelpText={false}
                    disabled={getIsFieldDisabled(
                      ProviderCode.KUBERNETES,
                      'kubeConfigContent',
                      isFormDisabled,
                      isProviderInUse
                    )}
                  />
                </FormField>
              )}
            </FieldGroup>
            <FieldGroup
              heading="Regions"
              headerAccessories={
                regions.length > 0 ? (
                  <RbacValidator accessRequiredOn={ApiPermissionMap.MODIFY_PROVIDER} isControl>
                    <YBButton
                      btnIcon="fa fa-plus"
                      btnText="Add Region"
                      btnClass="btn btn-default"
                      btnType="button"
                      onClick={showAddRegionFormModal}
                      disabled={getIsFieldDisabled(
                        ProviderCode.KUBERNETES,
                        'regions',
                        isFormDisabled,
                        isProviderInUse
                      )}
                      data-testid={`${FORM_NAME}-AddRegionButton`}
                    />
                  </RbacValidator>
                ) : null
              }
            >
              <RegionList
                providerCode={ProviderCode.KUBERNETES}
                providerOperation={ProviderOperation.EDIT}
                providerUuid={providerConfig.uuid}
                regions={regions}
                existingRegions={existingRegions}
                setRegionSelection={setRegionSelection}
                showAddRegionFormModal={showAddRegionFormModal}
                showEditRegionFormModal={showEditRegionFormModal}
                showDeleteRegionModal={showDeleteRegionModal}
                isDisabled={getIsFieldDisabled(
                  ProviderCode.KUBERNETES,
                  'regions',
                  isFormDisabled,
                  isProviderInUse
                )}
                isError={!!formMethods.formState.errors.regions}
                errors={formMethods.formState.errors.regions as any}
                linkedUniverses={linkedUniverses}
                isEditInUseProviderEnabled={isEditInUseProviderEnabled}
              />
              {formMethods.formState.errors.regions?.message && (
                <FormHelperText error={true}>
                  {formMethods.formState.errors.regions?.message}
                </FormHelperText>
              )}
            </FieldGroup>
            {(formMethods.formState.isValidating || formMethods.formState.isSubmitting) && (
              <SubmitInProgress isValidationEnabled={isValidationEnabled} />
            )}
          </Box>
          <Box marginTop="16px">
            <RbacValidator
              accessRequiredOn={ApiPermissionMap.MODIFY_PROVIDER}
              isControl
              overrideStyle={{ float: 'right' }}
            >
              <YBButton
                btnText={isValidationEnabled ? 'Validate and Apply Changes' : 'Apply Changes'}
                btnClass="btn btn-default save-btn"
                btnType="submit"
                disabled={isFormDisabled || formMethods.formState.isValidating}
                data-testid={`${FORM_NAME}-SubmitButton`}
              />
            </RbacValidator>
            {isValidationEnabled && isValidationErrorExist && (
              <RbacValidator
                accessRequiredOn={ApiPermissionMap.MODIFY_PROVIDER}
                isControl
                overrideStyle={{ float: 'right' }}
              >
                <YBButton
                  btnText="Ignore and save provider configuration anyway"
                  btnClass="btn btn-default float-right mr-10"
                  onClick={skipValidationAndSubmit}
                  disabled={isFormDisabled || formMethods.formState.isValidating}
                  data-testid={`${FORM_NAME}-IgnoreAndSave`}
                />
              </RbacValidator>
            )}
            <YBButton
              btnText="Clear Changes"
              btnClass="btn btn-default"
              onClick={(e: any) => {
                onFormReset();
                e.currentTarget.blur();
              }}
              disabled={isFormDisabled}
              data-testid={`${FORM_NAME}-ClearButton`}
            />
          </Box>
        </FormContainer>
      </FormProvider>
      {isRegionFormModalOpen && (
        <ConfigureK8sRegionModal
          configuredRegions={regions}
          isProviderFormDisabled={getIsFieldDisabled(
            ProviderCode.KUBERNETES,
            'regions',
            isFormDisabled,
            isProviderInUse
          )}
          inUseZones={inUseZones}
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
  providerConfig: K8sProvider
): Partial<K8sProviderEditFormFieldValues> => {
  return {
    dbNodePublicInternetAccess: !providerConfig.details.airGapInstall,
    editKubeConfigContent: false,
    editPullSecretContent: false,
    kubeConfigName: providerConfig.details.cloudInfo.kubernetes.kubeConfigName,
    kubernetesImageRegistry: providerConfig.details.cloudInfo.kubernetes.kubernetesImageRegistry,
    kubernetesProvider: {
      value: providerConfig.details.cloudInfo.kubernetes.kubernetesProvider,
      label: KubernetesProviderLabel[providerConfig.details.cloudInfo.kubernetes.kubernetesProvider]
    },
    providerName: providerConfig.name,
    regions: providerConfig.regions.map((region) => ({
      fieldId: generateLowerCaseAlphanumericId(),
      code: region.code,
      name: region.name,
      regionData: {
        value: { code: region.code, zoneOptions: [] },
        label: region.name
      },
      zones: region.zones.map((zone) => ({
        code: zone.code,
        certIssuerType: zone.details?.cloudInfo.kubernetes
          ? getCertIssuerType(zone.details?.cloudInfo.kubernetes)
          : K8sCertIssuerType.NONE,
        ...(zone.details?.cloudInfo.kubernetes && {
          certIssuerName:
            zone.details?.cloudInfo.kubernetes.certManagerClusterIssuer ??
            zone.details?.cloudInfo.kubernetes.certManagerIssuer,
          kubeConfigFilepath: zone.details.cloudInfo.kubernetes.kubeConfig,
          kubeDomain: zone.details.cloudInfo.kubernetes.kubeDomain,
          kubeNamespace: zone.details.cloudInfo.kubernetes.kubeNamespace,
          kubePodAddressTemplate: zone.details.cloudInfo.kubernetes.kubePodAddressTemplate,
          kubernetesStorageClass: zone.details.cloudInfo.kubernetes.kubernetesStorageClass,
          overrides: zone.details.cloudInfo.kubernetes.overrides
        })
      }))
    })),
    version: providerConfig.version
  };
};

const constructProviderPayload = async (
  formValues: K8sProviderEditFormFieldValues,
  providerConfig: K8sProvider
): Promise<YBProviderMutation> => {
  let kubernetesPullSecretContent = '';
  try {
    kubernetesPullSecretContent = formValues.kubernetesPullSecretContent
      ? (await readFileAsText(formValues.kubernetesPullSecretContent)) ?? ''
      : '';
  } catch (error) {
    throw new Error(`An error occurred while processing the pull secret file: ${error}`);
  }

  let kubernetesImagePullSecretName = '';
  try {
    // Type cast is required since JsYaml.load doesn't know the type of the input file
    const kubernetesPullSecretYAML = JsYaml.load(kubernetesPullSecretContent) as K8sPullSecretFile;
    kubernetesImagePullSecretName = kubernetesPullSecretYAML?.metadata?.name ?? '';
  } catch (error) {
    throw new Error(`An error occurred while reading the pull secret file as YAML: ${error}`);
  }

  let kubeConfigContent = '';
  try {
    kubeConfigContent = formValues.kubeConfigContent
      ? (await readFileAsText(formValues.kubeConfigContent)) ?? ''
      : '';
  } catch (error) {
    throw new Error(`An error occurred while processing the kube config file: ${error}`);
  }

  let regions = [];
  try {
    regions = await Promise.all(
      formValues.regions.map<Promise<K8sRegionMutation>>(async (regionFormValues) => {
        const existingRegion = findExistingRegion<K8sProvider, K8sRegion>(
          providerConfig,
          regionFormValues.code
        );

        // Preprocess the zones data collected from the form.
        // Store the zones data in a format compatiable with expected API payload.
        const preprocessedZones = await Promise.all(
          regionFormValues.zones.map<Promise<K8sAvailabilityZoneMutation>>(async (azFormValues) => {
            const existingZone = findExistingZone<K8sRegion, K8sAvailabilityZone>(
              existingRegion,
              azFormValues.code
            );

            // When the current az has no kubeConfig or user indicates they are editing the existing kubeConfig,
            // we need to take kubeConfig from the form as the input for the edit provider request.
            // Note: kubeConfig of `''` is considered as a special case on the YBA backend. This indicates that the user wants
            // to use the service account configs. This is why we are checking for `undefined` instead of falsy on the existing zone.
            const shouldReadKubeConfigOnForm =
              existingZone?.details?.cloudInfo.kubernetes.kubeConfig === undefined ||
              azFormValues.editKubeConfigContent;

            return {
              ...existingZone,
              code: azFormValues.code,
              name: azFormValues.code,
              details: {
                cloudInfo: {
                  [ProviderCode.KUBERNETES]: {
                    ...existingZone?.details?.cloudInfo?.kubernetes,
                    ...(shouldReadKubeConfigOnForm
                      ? {
                          ...(azFormValues.kubeConfigContent && {
                            kubeConfigContent:
                              (await readFileAsText(azFormValues.kubeConfigContent)) ?? '',
                            ...(azFormValues.kubeConfigContent.name && {
                              kubeConfigName: azFormValues.kubeConfigContent.name
                            })
                          })
                        }
                      : {
                          // YBA backend has special handling for kubeConfig. It is possibly `''` to indicate
                          // the user wants to use service account configs. This is why we're not dropping `''` strings here.
                          kubeConfig: existingZone?.details?.cloudInfo.kubernetes.kubeConfig
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
            };
          })
        );

        const newRegion: K8sRegionMutation = {
          ...existingRegion,
          code: regionFormValues.regionData.value.code,
          name: regionFormValues.regionData.label,
          zones: [
            ...preprocessedZones,
            ...getDeletedZones(existingRegion?.zones, regionFormValues.zones)
          ] as K8sAvailabilityZoneMutation[]
        };
        return newRegion;
      })
    );
  } catch (error) {
    throw new Error(
      `An error occurred while processing the zone level kube config files: ${error}`
    );
  }

  const {
    kubernetesImagePullSecretName: existingKubernetesImagePullSecretName,
    kubernetesPullSecret: existingKubernetesPullSecret,
    kubernetesPullSecretName: existingKubernetesPullSecretName
  } = providerConfig.details.cloudInfo.kubernetes;
  const { airGapInstall, cloudInfo, ...unexposedProviderDetailFields } = providerConfig.details;
  return {
    code: ProviderCode.KUBERNETES,
    name: formValues.providerName,
    details: {
      ...unexposedProviderDetailFields,
      airGapInstall: !formValues.dbNodePublicInternetAccess,
      cloudInfo: {
        [ProviderCode.KUBERNETES]: {
          ...(formValues.editKubeConfigContent
            ? {
                ...(formValues.kubeConfigContent && { kubeConfigContent: kubeConfigContent }),
                ...(formValues.kubeConfigContent?.name && {
                  kubeConfigName: formValues.kubeConfigContent.name
                })
              }
            : {
                // YBA backend has special handling for kubeConfig. It is possibly `''` to indicate
                // the user wants to use service account configs. This is why we're not dropping `''` strings here.
                kubeConfig: providerConfig.details.cloudInfo.kubernetes.kubeConfig
              }),
          kubernetesImageRegistry: formValues.kubernetesImageRegistry,
          kubernetesProvider: formValues.kubernetesProvider.value,
          ...(formValues.editPullSecretContent
            ? {
                ...(formValues.kubernetesPullSecretContent && {
                  kubernetesPullSecretContent: kubernetesPullSecretContent
                }),
                ...(formValues.kubernetesPullSecretContent?.name && {
                  kubernetesPullSecretName: formValues.kubernetesPullSecretContent.name
                }),
                ...(kubernetesImagePullSecretName && {
                  kubernetesImagePullSecretName: kubernetesImagePullSecretName
                })
              }
            : {
                ...(existingKubernetesPullSecret && {
                  kubernetesPullSecret: existingKubernetesPullSecret
                }),
                ...(existingKubernetesPullSecretName && {
                  kubernetesPullSecretName: existingKubernetesPullSecretName
                }),
                ...(existingKubernetesImagePullSecretName && {
                  kubernetesImagePullSecretName: existingKubernetesImagePullSecretName
                })
              })
        }
      }
    },
    regions: [
      ...regions,
      ...getDeletedRegions(providerConfig.regions, formValues.regions)
    ] as K8sRegionMutation[],
    version: formValues.version
  };
};
