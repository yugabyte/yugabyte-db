import { useState } from 'react';
import { FormProvider, SubmitHandler, useForm } from 'react-hook-form';
import { Box, FormHelperText, Typography } from '@material-ui/core';
import { yupResolver } from '@hookform/resolvers/yup';
import { array, mixed, object, string } from 'yup';
import { toast } from 'react-toastify';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';

import {
  RadioGroupOrientation,
  YBInput,
  YBInputField,
  YBRadioGroupField,
  YBToggleField
} from '../../../../../redesign/components';
import { YBButton } from '../../../../common/forms/fields';
import {
  CloudVendorRegionField,
  ConfigureRegionModal
} from '../configureRegion/ConfigureRegionModal';
import { NTPConfigField } from '../../components/NTPConfigField';
import { RegionList } from '../../components/RegionList';
import {
  KeyPairManagement,
  KEY_PAIR_MANAGEMENT_OPTIONS,
  NTPSetupType,
  ProviderCode,
  ProviderOperation,
  SshPrivateKeyInputType,
  VPCSetupType
} from '../../constants';
import { FieldGroup } from '../components/FieldGroup';
import { FormContainer } from '../components/FormContainer';
import { FormField } from '../components/FormField';
import { FieldLabel } from '../components/FieldLabel';
import { SubmitInProgress } from '../components/SubmitInProgress';
import {
  findExistingRegion,
  findExistingZone,
  getDeletedRegions,
  getDeletedZones,
  getInUseAzs,
  getLatestAccessKey,
  getNtpSetupType
} from '../../utils';
import { RegionOperation } from '../configureRegion/constants';
import {
  addItem,
  constructAccessKeysEditPayload,
  deleteItem,
  editItem,
  generateLowerCaseAlphanumericId,
  getIsFieldDisabled,
  getIsFormDisabled,
  readFileAsText,
  handleFormSubmitServerError,
  useIsProviderValidationEnabled
} from '../utils';
import { EditProvider } from '../ProviderEditView';
import { DeleteRegionModal } from '../../components/DeleteRegionModal';
import { VersionWarningBanner } from '../components/VersionWarningBanner';
import { ACCEPTABLE_CHARS } from '../../../../config/constants';
import { NTP_SERVER_REGEX } from '../constants';
import { getRegionOption, getRegionOptions } from '../configureRegion/utils';
import { ReactSelectOption, YBReactSelectField } from '../../components/YBReactSelect/YBReactSelectField';

import { UniverseItem } from '../../providerView/providerDetails/UniverseTable';
import { RuntimeConfigKey } from '../../../../../redesign/helpers/constants';
import { YBErrorIndicator, YBLoading } from '../../../../common/indicators';
import { api, regionMetadataQueryKey, runtimeConfigQueryKey } from '../../../../../redesign/helpers/api';
import {
  ImageBundle,
  OCIAvailabilityZone,
  OCIAvailabilityZoneMutation,
  OCIProvider,
  OCIRegion,
  OCIRegionMutation,
  RegionMetadataResponse,
  YBProviderMutation
} from '../../types';
import {
  hasNecessaryPerm,
  RbacValidator
} from '../../../../../redesign/features/rbac/common/RbacApiPermValidator';
import {
  ConfigureSSHDetailsMsg,
  IsOsPatchingEnabled,
  constructImageBundlePayload
} from '../../components/linuxVersionCatalog/LinuxVersionUtils';
import { ApiPermissionMap } from '../../../../../redesign/features/rbac/ApiAndUserPermMapping';
import { LinuxVersionCatalog } from '../../components/linuxVersionCatalog/LinuxVersionCatalog';
import { CloudType } from '../../../../../redesign/helpers/dtos';
import { OCI_FORM_MAPPERS, OCID_REGEX, OCI_FINGERPRINT_REGEX } from './constants';
import { OciApiPrivateKeyField } from '../../components/OciApiPrivateKeyField';
import { SshPrivateKeyFormField } from '../../components/SshPrivateKeyField';

interface OCIProviderEditFormProps {
  editProvider: EditProvider;
  linkedUniverses: UniverseItem[];
  providerConfig: OCIProvider;
}

export interface OCIProviderEditFormFieldValues {
  ociTenancyId: string;
  ociUserId: string;
  ociFingerprint: string;
  ociPrivateKeyContent: File;
  ociCompartmentId: string;
  ociRegionData: ReactSelectOption | null;
  ociHostedZoneId: string;
  dbNodePublicInternetAccess: boolean;
  editOciPrivateKey: boolean;
  editSSHKeypair: boolean;
  ntpServers: string[];
  ntpSetupType: NTPSetupType;
  providerName: string;
  imageBundles: ImageBundle[];
  regions: CloudVendorRegionField[];
  sshKeypairManagement: KeyPairManagement;
  sshKeypairName: string;
  sshPort: number | null;
  sshPrivateKeyInputType: SshPrivateKeyInputType;
  sshPrivateKeyContentText: string;
  sshPrivateKeyContent: File;
  sshUser: string;
  version: number;
}

const VALIDATION_SCHEMA = object().shape({
  providerName: string()
    .required('Provider Name is required.')
    .matches(
      ACCEPTABLE_CHARS,
      'Provider name cannot contain special characters other than "-", and "_"'
    ),
  ociTenancyId: string()
    .required('Tenancy OCID is required.')
    .matches(OCID_REGEX, 'Tenancy OCID must be in OCID format: ocid1.<type>.<realm>.<unique_id>'),
  ociUserId: string()
    .required('User OCID is required.')
    .matches(OCID_REGEX, 'User OCID must be in OCID format: ocid1.<type>.<realm>.<unique_id>'),
  ociFingerprint: string()
    .required('Fingerprint is required.')
    .matches(OCI_FINGERPRINT_REGEX, 'Fingerprint must be 16 colon-separated hexadecimal pairs'),
  ociPrivateKeyContent: mixed().when('editOciPrivateKey', {
    is: true,
    then: mixed().required('API private key is required.')
  }),
  ociCompartmentId: string()
    .required('Compartment OCID is required.')
    .matches(
      OCID_REGEX,
      'Compartment OCID must be in OCID format: ocid1.<type>.<realm>.<unique_id>'
    ),
  ociRegionData: object().required('Default region is required.'),
  ociHostedZoneId: string().matches(OCID_REGEX, {
    message: 'DNS Zone OCID must be in OCID format: ocid1.<type>.<realm>.<unique_id>',
    excludeEmptyString: true
  }),
  sshKeypairManagement: mixed().when('editSSHKeypair', {
    is: true,
    then: mixed().oneOf(
      [KeyPairManagement.SELF_MANAGED, KeyPairManagement.YBA_MANAGED],
      'SSH Keypair management choice is required.'
    )
  }),
  sshPrivateKeyContent: mixed().when(
    ['editSSHKeypair', 'sshKeypairManagement', 'sshPrivateKeyInputType'],
    {
      is: (editSSHKeypair, sshKeypairManagement, sshPrivateKeyInputType) =>
        editSSHKeypair &&
        sshKeypairManagement === KeyPairManagement.SELF_MANAGED &&
        sshPrivateKeyInputType === SshPrivateKeyInputType.UPLOAD_KEY,
      then: mixed().required('SSH private key is required.')
    }
  ),
  sshPrivateKeyContentText: string().when(
    ['editSSHKeypair', 'sshKeypairManagement', 'sshPrivateKeyInputType'],
    {
      is: (editSSHKeypair, sshKeypairManagement, sshPrivateKeyInputType) =>
        editSSHKeypair &&
        sshKeypairManagement === KeyPairManagement.SELF_MANAGED &&
        sshPrivateKeyInputType === SshPrivateKeyInputType.PASTE_KEY,
      then: string().required('SSH private key is required.')
    }
  ),
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

const FORM_NAME = 'OCIProviderEditForm';

export const OCIProviderEditForm = ({
  editProvider,
  linkedUniverses,
  providerConfig
}: OCIProviderEditFormProps) => {
  const [isRegionFormModalOpen, setIsRegionFormModalOpen] = useState<boolean>(false);
  const [isDeleteRegionModalOpen, setIsDeleteRegionModalOpen] = useState<boolean>(false);
  const [regionSelection, setRegionSelection] = useState<CloudVendorRegionField>();
  const [regionOperation, setRegionOperation] = useState<RegionOperation>(RegionOperation.ADD);
  const [isValidationErrorExist, setValidationErrorExist] = useState(false);

  const { t } = useTranslation();
  const initialDefaultValues = constructDefaultFormValues(providerConfig);
  const formMethods = useForm<OCIProviderEditFormFieldValues>({
    defaultValues: initialDefaultValues,
    resolver: yupResolver(VALIDATION_SCHEMA)
  });

  const customerUUID = localStorage.getItem('customerId') ?? '';
  const customerRuntimeConfigQuery = useQuery(
    runtimeConfigQueryKey.customerScope(customerUUID),
    () => api.fetchRuntimeConfigs(customerUUID, true)
  );
  const { isRuntimeConfigLoading, isValidationEnabled } = useIsProviderValidationEnabled(
    CloudType.oci
  );
  const regionMetadataQuery = useQuery(regionMetadataQueryKey.detail(ProviderCode.OCI), () =>
    api.fetchRegionMetadata(ProviderCode.OCI)
  );

  const isOsPatchingEnabled = IsOsPatchingEnabled();
  const sshConfigureMsg = ConfigureSSHDetailsMsg();

  if (customerRuntimeConfigQuery.isError) {
    return (
      <YBErrorIndicator
        customErrorMessage={t('failedToFetchCustomerRuntimeConfig', { keyPrefix: 'queryError' })}
      />
    );
  }
  if (
    customerRuntimeConfigQuery.isLoading ||
    regionMetadataQuery.isLoading ||
    isRuntimeConfigLoading
  ) {
    return <YBLoading />;
  }
  if (regionMetadataQuery.isError) {
    return <YBErrorIndicator customErrorMessage="Error fetching region metadata." />;
  }

  if (
    formMethods.formState.defaultValues?.ociRegionData === null &&
    regionMetadataQuery.data
  ) {
    formMethods.reset(constructDefaultFormValues(providerConfig, regionMetadataQuery.data));
  }

  const regionOptions = getRegionOptions(regionMetadataQuery.data!);

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

  const onFormReset = () => {
    formMethods.reset(
      constructDefaultFormValues(providerConfig, regionMetadataQuery.data)
    );
  };
  const onFormSubmit = async (
    formValues: OCIProviderEditFormFieldValues,
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
                OCI_FORM_MAPPERS
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

  const onFormValidateAndSubmit: SubmitHandler<OCIProviderEditFormFieldValues> = async (
    formValues
  ) => await onFormSubmit(formValues, isValidationEnabled);
  const onFormForceSubmit: SubmitHandler<OCIProviderEditFormFieldValues> = async (formValues) =>
    await onFormSubmit(formValues, isValidationEnabled, true);

  const skipValidationAndSubmit = () => {
    onFormForceSubmit(formMethods.getValues());
  };

  const regions = formMethods.watch('regions', initialDefaultValues.regions);
  const setRegions = (regions: CloudVendorRegionField[]) =>
    formMethods.setValue('regions', regions, { shouldValidate: true });
  const onRegionFormSubmit = (currentRegion: CloudVendorRegionField) => {
    regionOperation === RegionOperation.ADD
      ? addItem(currentRegion, regions, setRegions)
      : editItem(currentRegion, regions, setRegions);
  };
  const onDeleteRegionSubmit = (currentRegion: CloudVendorRegionField) =>
    deleteItem(currentRegion, regions, setRegions);

  const currentProviderVersion = formMethods.watch('version', initialDefaultValues.version);
  const keyPairManagement = formMethods.watch('sshKeypairManagement');
  const editSSHKeypair = formMethods.watch('editSSHKeypair', initialDefaultValues.editSSHKeypair);
  const editOciPrivateKey = formMethods.watch('editOciPrivateKey', initialDefaultValues.editOciPrivateKey);
  const latestAccessKey = getLatestAccessKey(providerConfig.allAccessKeys);
  const existingRegions = providerConfig.regions.map((region) => region.code);
  const runtimeConfigEntries = customerRuntimeConfigQuery.data.configEntries ?? [];
  const inUseZones = getInUseAzs(providerConfig.uuid, linkedUniverses, regionSelection?.code);
  const isEditInUseProviderEnabled = runtimeConfigEntries.some(
    (config: any) =>
      config.key === RuntimeConfigKey.EDIT_IN_USE_PROVIDER_UI_FEATURE_FLAG &&
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
          name="ociProviderForm"
          onSubmit={formMethods.handleSubmit(onFormValidateAndSubmit)}
        >
          {currentProviderVersion < providerConfig.version && (
            <VersionWarningBanner onReset={onFormReset} dataTestIdPrefix={FORM_NAME} />
          )}
          <Typography variant="h3">Manage OCI Provider Configuration</Typography>
          <FormField providerNameField={true}>
            <FieldLabel>Provider Name</FieldLabel>
            <YBInputField
              control={formMethods.control}
              name="providerName"
              disabled={getIsFieldDisabled(
                ProviderCode.OCI,
                'providerName',
                isFormDisabled,
                isProviderInUse
              )}
              fullWidth
            />
          </FormField>
          <Box width="100%" display="flex" flexDirection="column" gridGap="32px">
            <FieldGroup heading="Cloud Info">
              <FormField>
                <FieldLabel>Tenancy OCID</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="ociTenancyId"
                  disabled={getIsFieldDisabled(
                    ProviderCode.OCI,
                    'ociTenancyId',
                    isFormDisabled,
                    isProviderInUse
                  )}
                  fullWidth
                />
              </FormField>
              <FormField>
                <FieldLabel>User OCID</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="ociUserId"
                  disabled={getIsFieldDisabled(
                    ProviderCode.OCI,
                    'ociUserId',
                    isFormDisabled,
                    isProviderInUse
                  )}
                  fullWidth
                />
              </FormField>
              <FormField>
                <FieldLabel>Fingerprint</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="ociFingerprint"
                  disabled={getIsFieldDisabled(
                    ProviderCode.OCI,
                    'ociFingerprint',
                    isFormDisabled,
                    isProviderInUse
                  )}
                  fullWidth
                />
              </FormField>
              <FormField>
                <FieldLabel>Current API Private Key</FieldLabel>
                <YBInput
                  value={providerConfig.details.cloudInfo.oci.ociPrivateKeyContent}
                  disabled={true}
                  fullWidth
                />
              </FormField>
              <FormField>
                <FieldLabel>Change OCI API Private Key</FieldLabel>
                <YBToggleField
                  name="editOciPrivateKey"
                  control={formMethods.control}
                  disabled={getIsFieldDisabled(
                    ProviderCode.OCI,
                    'editOciPrivateKey',
                    isFormDisabled,
                    isProviderInUse
                  )}
                />
              </FormField>
              {editOciPrivateKey && (
                <OciApiPrivateKeyField isFormDisabled={isFormDisabled} isProviderInUse={isProviderInUse} />
              )}
              <FormField>
                <FieldLabel>Compartment OCID</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="ociCompartmentId"
                  disabled={getIsFieldDisabled(
                    ProviderCode.OCI,
                    'ociCompartmentId',
                    isFormDisabled,
                    isProviderInUse
                  )}
                  fullWidth
                />
              </FormField>
              <FormField>
                <FieldLabel>Default Region</FieldLabel>
                <YBReactSelectField
                  control={formMethods.control}
                  name="ociRegionData"
                  options={regionOptions}
                  isDisabled={isFormDisabled}
                  placeholder="Select region..."
                />
              </FormField>
              <FormField>
                <FieldLabel>DNS Zone OCID (Optional)</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="ociHostedZoneId"
                  disabled={getIsFieldDisabled(
                    ProviderCode.OCI,
                    'ociHostedZoneId',
                    isFormDisabled,
                    isProviderInUse
                  )}
                  fullWidth
                />
              </FormField>
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
                      disabled={isFormDisabled}
                      data-testid={`${FORM_NAME}-AddRegionButton`}
                    />
                  </RbacValidator>
                ) : null
              }
            >
              <RegionList
                providerCode={ProviderCode.OCI}
                providerOperation={ProviderOperation.EDIT}
                regions={regions}
                setRegionSelection={setRegionSelection}
                showAddRegionFormModal={showAddRegionFormModal}
                showEditRegionFormModal={showEditRegionFormModal}
                showDeleteRegionModal={showDeleteRegionModal}
                isDisabled={isFormDisabled}
                isError={!!formMethods.formState.errors.regions}
                errors={formMethods.formState.errors.regions as any}
                existingRegions={existingRegions}
              />
              {formMethods.formState.errors.regions?.message && (
                <FormHelperText error={true}>
                  {formMethods.formState.errors.regions?.message}
                </FormHelperText>
              )}
            </FieldGroup>
            <LinuxVersionCatalog
              control={formMethods.control as any}
              providerType={ProviderCode.OCI}
              providerOperation={ProviderOperation.EDIT}
              providerStatus={providerConfig.usabilityState}
              linkedUniverses={linkedUniverses}
              isDisabled={getIsFieldDisabled(
                ProviderCode.OCI,
                'imageBundles',
                isFormDisabled,
                isProviderInUse
              )}
            />
            <FieldGroup heading="SSH Key Pairs">
              {sshConfigureMsg}
              <FormField>
                <FieldLabel>Current SSH Key</FieldLabel>
                <YBInput value={latestAccessKey?.keyInfo.keyPairName ?? ''} disabled={true} fullWidth />
              </FormField>
              <FormField>
                <FieldLabel>Change SSH Keypair</FieldLabel>
                <YBToggleField
                  name="editSSHKeypair"
                  control={formMethods.control}
                  disabled={getIsFieldDisabled(
                    ProviderCode.OCI,
                    'editSSHKeypair',
                    isFormDisabled,
                    isProviderInUse
                  )}
                />
              </FormField>
              {editSSHKeypair && (
                <>
                  <FormField>
                    <FieldLabel>SSH User</FieldLabel>
                    <YBInputField
                      control={formMethods.control}
                      name="sshUser"
                      disabled={
                        getIsFieldDisabled(
                          ProviderCode.OCI,
                          'sshUser',
                          isFormDisabled,
                          isProviderInUse
                        ) || isOsPatchingEnabled
                      }
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
                      disabled={
                        getIsFieldDisabled(
                          ProviderCode.OCI,
                          'sshPort',
                          isFormDisabled,
                          isProviderInUse
                        ) || isOsPatchingEnabled
                      }
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
                      isDisabled={getIsFieldDisabled(
                        ProviderCode.OCI,
                        'sshKeypairManagement',
                        isFormDisabled,
                        isProviderInUse
                      )}
                    />
                  </FormField>
                  {keyPairManagement === KeyPairManagement.SELF_MANAGED && (
                    <>
                      <FormField>
                        <FieldLabel>SSH Keypair Name</FieldLabel>
                        <YBInputField
                          control={formMethods.control}
                          name="sshKeypairName"
                          disabled={getIsFieldDisabled(
                            ProviderCode.OCI,
                            'sshKeypairName',
                            isFormDisabled,
                            isProviderInUse
                          )}
                          fullWidth
                        />
                      </FormField>
                      <SshPrivateKeyFormField
                        isFormDisabled={isFormDisabled}
                        providerCode={ProviderCode.OCI}
                        isProviderInUse={isProviderInUse}
                      />
                    </>
                  )}
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
                  disabled={getIsFieldDisabled(
                    ProviderCode.OCI,
                    'dbNodePublicInternetAccess',
                    isFormDisabled,
                    isProviderInUse
                  )}
                />
              </FormField>
              <FormField>
                <FieldLabel>NTP Setup</FieldLabel>
                <NTPConfigField
                  isDisabled={getIsFieldDisabled(
                    ProviderCode.OCI,
                    'ntpSetupType',
                    isFormDisabled,
                    isProviderInUse
                  )}
                  providerCode={ProviderCode.OCI}
                />
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
                  : 'Save Provider Configuration'
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
          </Box>
        </FormContainer>
      </FormProvider>
      {isRegionFormModalOpen && (
        <ConfigureRegionModal
          configuredRegions={regions}
          isEditProvider={true}
          isProviderFormDisabled={isFormDisabled}
          inUseZones={inUseZones}
          onClose={hideRegionFormModal}
          onRegionSubmit={onRegionFormSubmit}
          open={isRegionFormModalOpen}
          providerCode={ProviderCode.OCI}
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

const constructDefaultFormValues = (
  providerConfig: OCIProvider,
  regionMetadataResponse?: RegionMetadataResponse
): Partial<OCIProviderEditFormFieldValues> => ({
  ociTenancyId: providerConfig.details.cloudInfo.oci.ociTenancyId ?? '',
  ociUserId: providerConfig.details.cloudInfo.oci.ociUserId ?? '',
  ociFingerprint: providerConfig.details.cloudInfo.oci.ociFingerprint ?? '',
  ociCompartmentId: providerConfig.details.cloudInfo.oci.ociCompartmentId ?? '',
  ociRegionData: regionMetadataResponse
    ? getRegionOption(providerConfig.details.cloudInfo.oci.ociRegion, regionMetadataResponse)
    : null,
  ociHostedZoneId: providerConfig.details.cloudInfo.oci.ociHostedZoneId ?? '',
  dbNodePublicInternetAccess: !providerConfig.details.airGapInstall,
  editOciPrivateKey: false,
  editSSHKeypair: false,
  ntpServers: providerConfig.details.ntpServers,
  ntpSetupType: getNtpSetupType(providerConfig),
  providerName: providerConfig.name,
  imageBundles: providerConfig.imageBundles,
  regions: providerConfig.regions.map((region) => ({
    fieldId: generateLowerCaseAlphanumericId(),
    code: region.code,
    name: region.name,
    vnet: region.details?.cloudInfo?.oci?.vnet ?? '',
    zones: region.zones
  })),
  sshKeypairManagement: getLatestAccessKey(providerConfig.allAccessKeys)?.keyInfo.managementState,
  sshPrivateKeyInputType: SshPrivateKeyInputType.UPLOAD_KEY,
  sshPort: providerConfig.details.sshPort ?? null,
  sshUser: providerConfig.details.sshUser ?? '',
  version: providerConfig.version
});

const constructProviderPayload = async (
  formValues: OCIProviderEditFormFieldValues,
  providerConfig: OCIProvider
): Promise<YBProviderMutation> => {
  let ociPrivateKeyContent = '';
  if (formValues.editOciPrivateKey) {
    try {
      ociPrivateKeyContent = formValues.ociPrivateKeyContent
        ? (await readFileAsText(formValues.ociPrivateKeyContent)) ?? ''
        : '';
    } catch (error) {
      throw new Error(`An error occurred while processing the API private key file: ${error}`);
    }
  }

  let sshPrivateKeyContent = '';
  if (formValues.sshPrivateKeyInputType === SshPrivateKeyInputType.UPLOAD_KEY) {
    try {
      sshPrivateKeyContent = formValues.sshPrivateKeyContent
        ? (await readFileAsText(formValues.sshPrivateKeyContent)) ?? ''
        : '';
    } catch (error) {
      throw new Error(`An error occurred while processing the SSH private key file: ${error}`);
    }
  } else {
    sshPrivateKeyContent = formValues.sshPrivateKeyContentText;
  }

  const imageBundles = constructImageBundlePayload(formValues);

  const allAccessKeysPayload = constructAccessKeysEditPayload(
    formValues.editSSHKeypair,
    formValues.sshKeypairManagement,
    { sshKeypairName: formValues.sshKeypairName, sshPrivateKeyContent: sshPrivateKeyContent },
    providerConfig.allAccessKeys
  );

  const { cloudInfo, ...unexposedProviderDetailFields } = providerConfig.details;
  const { ociPrivateKeyContent: existingOciPrivateKeyContent, ...unexposedProviderCloudInfoFields } =
    cloudInfo.oci;

  return {
    code: ProviderCode.OCI,
    name: formValues.providerName,
    ...allAccessKeysPayload,
    details: {
      ...unexposedProviderDetailFields,
      airGapInstall: !formValues.dbNodePublicInternetAccess,
      cloudInfo: {
        [ProviderCode.OCI]: {
          ...unexposedProviderCloudInfoFields,
          ociTenancyId: formValues.ociTenancyId,
          ociUserId: formValues.ociUserId,
          ociFingerprint: formValues.ociFingerprint,
          ociPrivateKeyContent: formValues.editOciPrivateKey
            ? ociPrivateKeyContent
            : existingOciPrivateKeyContent,
          ociCompartmentId: formValues.ociCompartmentId,
          ociRegion: formValues.ociRegionData?.value?.code ?? '',
          ...(formValues.ociHostedZoneId && { ociHostedZoneId: formValues.ociHostedZoneId })
        }
      },
      ntpServers: formValues.ntpServers,
      setUpChrony: formValues.ntpSetupType !== NTPSetupType.NO_NTP,
      ...(formValues.sshPort && { sshPort: formValues.sshPort }),
      ...(formValues.sshUser && { sshUser: formValues.sshUser })
    },
    imageBundles,
    regions: [
      ...formValues.regions.map<OCIRegionMutation>((regionFormValues) => {
        const existingRegion = findExistingRegion<OCIProvider, OCIRegion>(
          providerConfig,
          regionFormValues.code
        );

        return {
          ...existingRegion,
          code: regionFormValues.code,
          details: {
            ...existingRegion?.details,
            cloudInfo: {
              [ProviderCode.OCI]: {
                ...existingRegion?.details.cloudInfo.oci,
                ...(regionFormValues.vnet && {
                  vnet: regionFormValues.vnet
                })
              }
            }
          },
          zones: [
            ...regionFormValues.zones.map<OCIAvailabilityZoneMutation>((azFormValues) => {
              const existingZone = findExistingZone<OCIRegion, OCIAvailabilityZone>(
                existingRegion,
                azFormValues.code
              );
              return {
                ...existingZone,
                code: azFormValues.code,
                name: azFormValues.code,
                subnet: azFormValues.subnet
              };
            }),
            ...getDeletedZones(existingRegion?.zones, regionFormValues.zones)
          ]
        };
      }),
      ...getDeletedRegions<OCIRegion, CloudVendorRegionField>(
        providerConfig.regions,
        formValues.regions
      )
    ],
    version: formValues.version
  };
};
