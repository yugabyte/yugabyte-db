import { useState } from 'react';
import { array, mixed, object, string } from 'yup';
import { Box, CircularProgress, FormHelperText, Typography } from '@material-ui/core';
import { FormProvider, SubmitHandler, useForm } from 'react-hook-form';
import { yupResolver } from '@hookform/resolvers/yup';
import { toast } from 'react-toastify';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import {
  RadioGroupOrientation,
  YBInputField,
  YBRadioGroupField,
  YBToggleField,
  OptionProps
} from '../../../../../redesign/components';
import { YBButton } from '../../../../common/forms/fields';
import {
  ConfigureRegionModal,
  CloudVendorRegionField
} from '../configureRegion/ConfigureRegionModal';
import { DeleteRegionModal } from '../../components/DeleteRegionModal';
import { NTPConfigField } from '../../components/NTPConfigField';
import { RegionList } from '../../components/RegionList';
import { YBDropZoneField } from '../../components/YBDropZone/YBDropZoneField';
import {
  DEFAULT_SSH_PORT,
  KeyPairManagement,
  KEY_PAIR_MANAGEMENT_OPTIONS,
  NTPSetupType,
  ProviderCode,
  ProviderOperation,
  VPCSetupType,
  AzuProviderCredentialType
} from '../../constants';
import { FieldGroup } from '../components/FieldGroup';
import {
  addItem,
  constructAccessKeysCreatePayload,
  deleteItem,
  editItem,
  getIsFormDisabled,
  readFileAsText,
  handleFormSubmitServerError,
  UseProviderValidationEnabled
} from '../utils';
import { FormContainer } from '../components/FormContainer';
import { ACCEPTABLE_CHARS } from '../../../../config/constants';
import { FormField } from '../components/FormField';
import { FieldLabel } from '../components/FieldLabel';
import { SubmitInProgress } from '../components/SubmitInProgress';
import { CreateInfraProvider } from '../../InfraProvider';
import { RegionOperation } from '../configureRegion/constants';
import { NTP_SERVER_REGEX } from '../constants';

import { fetchGlobalRunTimeConfigs } from '../../../../../api/admin';
import { QUERY_KEY } from '../../../../../redesign/features/universe/universe-form/utils/api';
import { AZURegionMutation, AZUAvailabilityZoneMutation, YBProviderMutation } from '../../types';
import { RbacValidator } from '../../../../../redesign/features/rbac/common/RbacApiPermValidator';
import {
  ConfigureSSHDetailsMsg,
  IsOsPatchingEnabled,
  constructImageBundlePayload
} from '../../components/linuxVersionCatalog/LinuxVersionUtils';
import { ApiPermissionMap } from '../../../../../redesign/features/rbac/ApiAndUserPermMapping';
import { LinuxVersionCatalog } from '../../components/linuxVersionCatalog/LinuxVersionCatalog';
import { CloudType } from '../../../../../redesign/helpers/dtos';
import { ImageBundle } from '../../../../../redesign/features/universe/universe-form/utils/dto';
import { getYBAHost } from '../../utils';
import { api, hostInfoQueryKey } from '../../../../../redesign/helpers/api';
import { YBErrorIndicator, YBLoading } from '../../../../common/indicators';
import { YBAHost } from '../../../../../redesign/helpers/constants';
import { AZURE_FORM_MAPPERS } from './constants';

interface AZUProviderCreateFormProps {
  createInfraProvider: CreateInfraProvider;
  onBack: () => void;
}

export interface AZUProviderCreateFormFieldValues {
  azuClientId: string;
  azuClientSecret: string;
  azuHostedZoneId: string;
  azuRG: string;
  azuNetworkRG: string;
  azuSubscriptionId: string;
  azuNetworkSubscriptionId: string;
  azuTenantId: string;
  dbNodePublicInternetAccess: boolean;
  ntpServers: string[];
  ntpSetupType: NTPSetupType;
  providerName: string;
  regions: CloudVendorRegionField[];
  sshKeypairManagement: KeyPairManagement;
  sshKeypairName: string;
  sshPort: number;
  sshPrivateKeyContent: File;
  sshUser: string;
  imageBundles: ImageBundle[];
  providerCredentialType: AzuProviderCredentialType;
}

export const DEFAULT_FORM_VALUES: Partial<AZUProviderCreateFormFieldValues> = {
  dbNodePublicInternetAccess: true,
  ntpServers: [] as string[],
  ntpSetupType: NTPSetupType.CLOUD_VENDOR,
  providerName: '',
  regions: [] as CloudVendorRegionField[],
  sshKeypairManagement: KeyPairManagement.YBA_MANAGED,
  sshPort: DEFAULT_SSH_PORT,
  providerCredentialType: AzuProviderCredentialType.SPECIFIED_SERVICE_PRINCIPAL
} as const;

const VALIDATION_SCHEMA = object().shape({
  providerName: string()
    .required('Provider Name is required.')
    .matches(
      ACCEPTABLE_CHARS,
      'Provider name cannot contain special characters other than "-", and "_"'
    ),
  azuClientId: string().required('Azure Client ID is required.'),
  azuClientSecret: mixed().when('providerCredentialType', {
    is: AzuProviderCredentialType.SPECIFIED_SERVICE_PRINCIPAL,
    then: mixed().required('Azure Client Secret is required.')
  }),
  azuRG: string().required('Azure Resource Group is required.'),
  azuSubscriptionId: string().required('Azure Subscription ID is required.'),
  azuTenantId: string().required('Azure Tenant ID is required.'),
  sshPrivateKeyContent: mixed().when('sshKeypairManagement', {
    is: KeyPairManagement.SELF_MANAGED,
    then: mixed().required('SSH private key is required.')
  }),
  hostedZoneId: string().when('enableHostedZone', {
    is: true,
    then: string().required('Route 53 zone id is required.')
  }),
  ntpServers: array().when('ntpSetupType', {
    is: NTPSetupType.SPECIFIED,
    then: array().of(
      string().matches(
        NTP_SERVER_REGEX,
        (testContext) =>
          `NTP servers must be provided in IPv4, IPv6, or hostname format. '${testContext.originalValue}' is not valid.`
      )
    )
  }),
  regions: array().min(1, 'Provider configurations must contain at least one region.')
});

const FORM_NAME = 'AZUProviderCreateForm';

export const AZUProviderCreateForm = ({
  onBack,
  createInfraProvider
}: AZUProviderCreateFormProps) => {
  const [isRegionFormModalOpen, setIsRegionFormModalOpen] = useState<boolean>(false);
  const [isDeleteRegionModalOpen, setIsDeleteRegionModalOpen] = useState<boolean>(false);
  const [regionSelection, setRegionSelection] = useState<CloudVendorRegionField>();
  const [regionOperation, setRegionOperation] = useState<RegionOperation>(RegionOperation.ADD);
  const [isValidationErrorExist, setValidationErrorExist] = useState(false);
  const { t } = useTranslation();
  const formMethods = useForm<AZUProviderCreateFormFieldValues>({
    defaultValues: DEFAULT_FORM_VALUES,
    resolver: yupResolver(VALIDATION_SCHEMA)
  });

  const globalRuntimeConfigQuery = useQuery(QUERY_KEY.fetchGlobalRunTimeConfigs, () =>
    fetchGlobalRunTimeConfigs(true).then((res: any) => res.data)
  );
  const {
    isLoading: isProviderValidationLoading,
    isValidationEnabled
  } = UseProviderValidationEnabled(CloudType.azu);
  const hostInfoQuery = useQuery(hostInfoQueryKey.ALL, () => api.fetchHostInfo());

  const isOsPatchingEnabled = IsOsPatchingEnabled();
  const sshConfigureMsg = ConfigureSSHDetailsMsg();

  if (
    hostInfoQuery.isLoading ||
    globalRuntimeConfigQuery.isLoading ||
    isProviderValidationLoading
  ) {
    return <YBLoading />;
  }

  if (hostInfoQuery.isError) {
    return (
      <YBErrorIndicator
        customErrorMessage={t('failedToFetchHostInfo', { keyPrefix: 'queryError' })}
      />
    );
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

  const onFormSubmit = async (
    formValues: AZUProviderCreateFormFieldValues,
    shouldValidate: boolean,
    ignoreValidationErrors = false
  ) => {
    if (formValues.ntpSetupType === NTPSetupType.SPECIFIED && !formValues.ntpServers.length) {
      formMethods.setError('ntpServers', {
        type: 'min',
        message: 'Please specify at least one NTP server.'
      });
      return;
    }

    try {
      setValidationErrorExist(false);
      const providerPayload = await constructProviderPayload(formValues);
      try {
        await createInfraProvider(providerPayload, {
          shouldValidate: shouldValidate,
          ignoreValidationErrors: ignoreValidationErrors,
          mutateOptions: {
            onError: (err) => {
              handleFormSubmitServerError(
                (err as any)?.response?.data,
                formMethods,
                AZURE_FORM_MAPPERS
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

  const onFormValidateAndSubmit: SubmitHandler<AZUProviderCreateFormFieldValues> = async (
    formValues
  ) => await onFormSubmit(formValues, isValidationEnabled);
  const onFormForceSubmit: SubmitHandler<AZUProviderCreateFormFieldValues> = async (formValues) =>
    await onFormSubmit(formValues, isValidationEnabled, true);

  const skipValidationAndSubmit = () => {
    onFormForceSubmit(formMethods.getValues());
  };

  const regions = formMethods.watch('regions', DEFAULT_FORM_VALUES.regions);
  const setRegions = (regions: CloudVendorRegionField[]) =>
    formMethods.setValue('regions', regions, { shouldValidate: true });
  const onRegionFormSubmit = (currentRegion: CloudVendorRegionField) => {
    regionOperation === RegionOperation.ADD
      ? addItem(currentRegion, regions, setRegions)
      : editItem(currentRegion, regions, setRegions);
  };
  const onDeleteRegionSubmit = (currentRegion: CloudVendorRegionField) =>
    deleteItem(currentRegion, regions, setRegions);

  const credentialOptions: OptionProps[] = [
    {
      value: AzuProviderCredentialType.SPECIFIED_SERVICE_PRINCIPAL,
      label: 'Specify Client Secret'
    },
    {
      value: AzuProviderCredentialType.HOST_INSTANCE_MI,
      label: `Use Managed Identity from this YBA host's instance`,
      disabled: hostInfoQuery.data === undefined || getYBAHost(hostInfoQuery.data) !== YBAHost.AZU
    }
  ];

  const providerCredentialType = formMethods.watch(
    'providerCredentialType',
    DEFAULT_FORM_VALUES.providerCredentialType
  );

  const keyPairManagement = formMethods.watch(
    'sshKeypairManagement',
    DEFAULT_FORM_VALUES.sshKeypairManagement
  );

  const isFormDisabled = getIsFormDisabled(formMethods.formState);
  return (
    <Box display="flex" justifyContent="center">
      <FormProvider {...formMethods}>
        <FormContainer
          name="azuProviderForm"
          onSubmit={formMethods.handleSubmit(onFormValidateAndSubmit)}
        >
          <Typography variant="h3">Create Azure Provider Configuration</Typography>
          <FormField providerNameField={true}>
            <FieldLabel>Provider Name</FieldLabel>
            <YBInputField
              control={formMethods.control}
              name="providerName"
              disabled={isFormDisabled}
              fullWidth
            />
          </FormField>
          <Box width="100%" display="flex" flexDirection="column" gridGap="32px">
            <FieldGroup heading="Cloud Info">
              <FormField>
                <FieldLabel>Client ID</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="azuClientId"
                  disabled={isFormDisabled}
                  fullWidth
                />
              </FormField>
              <FormField>
                <FieldLabel
                  infoTitle="Credential Type"
                  infoContent="For public cloud providers, YBA creates compute instances and therefore requires sufficient permissions to do so."
                >
                  Credential Type
                </FieldLabel>
                <YBRadioGroupField
                  name="providerCredentialType"
                  control={formMethods.control}
                  options={credentialOptions}
                  orientation={RadioGroupOrientation.HORIZONTAL}
                />
              </FormField>
              {providerCredentialType === AzuProviderCredentialType.SPECIFIED_SERVICE_PRINCIPAL && (
                <>
                  <FormField>
                    <FieldLabel>Client Secret</FieldLabel>
                    <YBInputField
                      control={formMethods.control}
                      name="azuClientSecret"
                      disabled={isFormDisabled}
                      fullWidth
                    />
                  </FormField>
                </>
              )}
              <FormField>
                <FieldLabel>Resource Group</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="azuRG"
                  disabled={isFormDisabled}
                  fullWidth
                />
              </FormField>
              <FormField>
                <FieldLabel
                  infoTitle="Network Resource Group"
                  infoContent="All network and NIC resources of VMs will be created in this group. If left empty, the default resource group will be used."
                >
                  Network Resource Group (Optional)
                </FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="azuNetworkRG"
                  disabled={isFormDisabled}
                  fullWidth
                />
              </FormField>
              <FormField>
                <FieldLabel>Subscription ID</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="azuSubscriptionId"
                  disabled={isFormDisabled}
                  fullWidth
                />
              </FormField>
              <FormField>
                <FieldLabel
                  infoTitle="Network Subscription ID"
                  infoContent="All network and NIC resources of VMs will be created under this subscription. If left empty, the default subscription id will be used."
                >
                  Network Subscription ID (Optional)
                </FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="azuNetworkSubscriptionId"
                  disabled={isFormDisabled}
                  fullWidth
                />
              </FormField>
              <FormField>
                <FieldLabel>Tenant ID</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="azuTenantId"
                  disabled={isFormDisabled}
                  fullWidth
                />
              </FormField>
              <FormField>
                <FieldLabel>Private DNS Zone (Optional)</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="azuHostedZoneId"
                  disabled={isFormDisabled}
                  fullWidth
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
                providerCode={ProviderCode.AZU}
                providerOperation={ProviderOperation.CREATE}
                regions={regions}
                setRegionSelection={setRegionSelection}
                showAddRegionFormModal={showAddRegionFormModal}
                showEditRegionFormModal={showEditRegionFormModal}
                showDeleteRegionModal={showDeleteRegionModal}
                isDisabled={isFormDisabled}
                isError={!!formMethods.formState.errors.regions}
                errors={formMethods.formState.errors.regions as any}
              />
              {formMethods.formState.errors.regions?.message && (
                <FormHelperText error={true}>
                  {formMethods.formState.errors.regions?.message}
                </FormHelperText>
              )}
            </FieldGroup>
            <LinuxVersionCatalog
              control={formMethods.control as any}
              providerType={ProviderCode.AZU}
              providerOperation={ProviderOperation.CREATE}
              isDisabled={isFormDisabled}
            />
            <FieldGroup heading="SSH Key Pairs">
              {sshConfigureMsg}
              <FormField>
                <FieldLabel>SSH User</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="sshUser"
                  disabled={isFormDisabled || isOsPatchingEnabled}
                  fullWidth
                />
              </FormField>
              <FormField>
                <FieldLabel>SSH Port</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="sshPort"
                  type="number"
                  inputProps={{ min: 1, max: 65535 }}
                  disabled={isFormDisabled || isOsPatchingEnabled}
                  fullWidth
                />
              </FormField>
              <FormField>
                <FieldLabel>Key Pair Management</FieldLabel>
                <YBRadioGroupField
                  name="sshKeypairManagement"
                  control={formMethods.control}
                  options={KEY_PAIR_MANAGEMENT_OPTIONS}
                  orientation={RadioGroupOrientation.HORIZONTAL}
                />
              </FormField>
              {keyPairManagement === KeyPairManagement.SELF_MANAGED && (
                <>
                  <FormField>
                    <FieldLabel>SSH Keypair Name</FieldLabel>
                    <YBInputField
                      control={formMethods.control}
                      name="sshKeypairName"
                      disabled={isFormDisabled}
                      fullWidth
                    />
                  </FormField>
                  <FormField>
                    <FieldLabel>SSH Private Key Content</FieldLabel>
                    <YBDropZoneField
                      name="sshPrivateKeyContent"
                      control={formMethods.control}
                      actionButtonText="Upload SSH Key PEM File"
                      multipleFiles={false}
                      showHelpText={false}
                      disabled={isFormDisabled}
                    />
                  </FormField>
                </>
              )}
            </FieldGroup>
            <FieldGroup heading="Advanced">
              <FormField>
                <FieldLabel
                  infoTitle="DB Nodes have public internet access?"
                  infoContent="If yes, YBA will install some software packages on the DB nodes by downloading from the public internet. If not, all installation of software on the nodes will download from only this YBA instance."
                >
                  DB Nodes have public internet access?
                </FieldLabel>
                <YBToggleField
                  name="dbNodePublicInternetAccess"
                  control={formMethods.control}
                  disabled={isFormDisabled}
                />
              </FormField>
              <FormField>
                <FieldLabel>NTP Setup</FieldLabel>
                <NTPConfigField isDisabled={isFormDisabled} providerCode={ProviderCode.AZU} />
              </FormField>
            </FieldGroup>
            {(formMethods.formState.isValidating || formMethods.formState.isSubmitting) && (
              <SubmitInProgress isValidationEnabled={isValidationEnabled} />
            )}
          </Box>
          <Box marginTop="16px">
            <YBButton
              btnText={
                isValidationEnabled
                  ? 'Validate and Save Configuration'
                  : 'Create Provider Configuration'
              }
              btnClass="btn btn-default save-btn"
              btnType="submit"
              disabled={isFormDisabled || formMethods.formState.isValidating}
              data-testid={`${FORM_NAME}-SubmitButton`}
            />
            {isValidationEnabled && isValidationErrorExist && (
              <YBButton
                btnText="Ignore and save provider configuration anyway"
                btnClass="btn btn-default float-right mr-10"
                onClick={skipValidationAndSubmit}
                disabled={isFormDisabled || formMethods.formState.isValidating}
                data-testid={`${FORM_NAME}-IgnoreAndSave`}
              />
            )}
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
      {/* Modals */}
      {isRegionFormModalOpen && (
        <ConfigureRegionModal
          configuredRegions={regions}
          isEditProvider={false}
          isProviderFormDisabled={isFormDisabled}
          onClose={hideRegionFormModal}
          onRegionSubmit={onRegionFormSubmit}
          open={isRegionFormModalOpen}
          providerCode={ProviderCode.AZU}
          regionOperation={regionOperation}
          regionSelection={regionSelection}
          vpcSetupType={VPCSetupType.EXISTING}
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

const constructProviderPayload = async (
  formValues: AZUProviderCreateFormFieldValues
): Promise<YBProviderMutation> => {
  let sshPrivateKeyContent = '';
  try {
    sshPrivateKeyContent = formValues.sshPrivateKeyContent
      ? (await readFileAsText(formValues.sshPrivateKeyContent)) ?? ''
      : '';
  } catch (error) {
    throw new Error(`An error occurred while processing the SSH private key file: ${error}`);
  }

  const imageBundles = constructImageBundlePayload(formValues);

  const allAccessKeysPayload = constructAccessKeysCreatePayload(
    formValues.sshKeypairManagement,
    formValues.sshKeypairName,
    sshPrivateKeyContent
  );
  return {
    code: ProviderCode.AZU,
    name: formValues.providerName,
    ...allAccessKeysPayload,
    details: {
      airGapInstall: !formValues.dbNodePublicInternetAccess,
      cloudInfo: {
        [ProviderCode.AZU]: {
          azuClientId: formValues.azuClientId,
          ...(formValues.providerCredentialType ===
            AzuProviderCredentialType.SPECIFIED_SERVICE_PRINCIPAL && {
            azuClientSecret: formValues.azuClientSecret
          }),
          ...(formValues.azuHostedZoneId && { azuHostedZoneId: formValues.azuHostedZoneId }),
          azuRG: formValues.azuRG,
          ...(formValues.azuNetworkRG && { azuNetworkRG: formValues.azuNetworkRG }),
          azuSubscriptionId: formValues.azuSubscriptionId,
          ...(formValues.azuNetworkSubscriptionId && {
            azuNetworkSubscriptionId: formValues.azuNetworkSubscriptionId
          }),
          azuTenantId: formValues.azuTenantId
        }
      },
      ntpServers: formValues.ntpServers,
      setUpChrony: formValues.ntpSetupType !== NTPSetupType.NO_NTP,
      ...(formValues.sshPort && { sshPort: formValues.sshPort }),
      ...(formValues.sshUser && { sshUser: formValues.sshUser })
    },
    regions: formValues.regions.map<AZURegionMutation>((regionFormValues) => ({
      code: regionFormValues.code,
      details: {
        cloudInfo: {
          [ProviderCode.AZU]: {
            ...(regionFormValues.securityGroupId && {
              securityGroupId: regionFormValues.securityGroupId
            }),
            ...(regionFormValues.vnet && {
              vnet: regionFormValues.vnet
            }),
            ...(regionFormValues.ybImage && {
              ybImage: regionFormValues.ybImage
            })
          }
        }
      },
      zones: regionFormValues.zones?.map<AZUAvailabilityZoneMutation>((azFormValues) => ({
        code: azFormValues.code,
        name: azFormValues.code,
        subnet: azFormValues.subnet
      }))
    })),
    imageBundles
  };
};
