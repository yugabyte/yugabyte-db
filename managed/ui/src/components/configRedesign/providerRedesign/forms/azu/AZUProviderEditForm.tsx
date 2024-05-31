import { useState } from 'react';
import { FormProvider, SubmitHandler, useForm } from 'react-hook-form';
import { Box, CircularProgress, FormHelperText, Typography } from '@material-ui/core';
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
  VPCSetupType
} from '../../constants';
import { FieldGroup } from '../components/FieldGroup';
import { FormContainer } from '../components/FormContainer';
import { FormField } from '../components/FormField';
import { FieldLabel } from '../components/FieldLabel';
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
  readFileAsText
} from '../utils';
import { EditProvider } from '../ProviderEditView';
import { DeleteRegionModal } from '../../components/DeleteRegionModal';
import { YBDropZoneField } from '../../components/YBDropZone/YBDropZoneField';
import { VersionWarningBanner } from '../components/VersionWarningBanner';
import { ACCEPTABLE_CHARS } from '../../../../config/constants';
import { NTP_SERVER_REGEX } from '../constants';
import { UniverseItem } from '../../providerView/providerDetails/UniverseTable';
import { RuntimeConfigKey } from '../../../../../redesign/helpers/constants';
import { YBErrorIndicator, YBLoading } from '../../../../common/indicators';
import { api, runtimeConfigQueryKey } from '../../../../../redesign/helpers/api';

import {
  AZUAvailabilityZone,
  AZUAvailabilityZoneMutation,
  AZUProvider,
  AZURegion,
  AZURegionMutation,
  ImageBundle,
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

interface AZUProviderEditFormProps {
  editProvider: EditProvider;
  linkedUniverses: UniverseItem[];
  providerConfig: AZUProvider;
}

export interface AZUProviderEditFormFieldValues {
  azuClientId: string;
  azuClientSecret: string;
  azuHostedZoneId: string;
  azuRG: string;
  azuNetworkRG: string;
  azuSubscriptionId: string;
  azuNetworkSubscriptionId: string;
  azuTenantId: string;
  dbNodePublicInternetAccess: boolean;
  editSSHKeypair: boolean;
  ntpServers: string[];
  ntpSetupType: NTPSetupType;
  providerName: string;
  imageBundles: ImageBundle[];
  regions: CloudVendorRegionField[];
  sshKeypairManagement: KeyPairManagement;
  sshKeypairName: string;
  sshPort: number | null;
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
  azuClientId: string().required('Azure Client ID is required.'),
  azuClientSecret: string().required('Azure Client Secret is required.'),
  azuRG: string().required('Azure Resource Group is required.'),
  azuSubscriptionId: string().required('Azure Subscription ID is required.'),
  azuTenantId: string().required('Azure Tenant ID is required.'),
  sshKeypairManagement: mixed().when('editSSHKeypair', {
    is: true,
    then: mixed().oneOf(
      [KeyPairManagement.SELF_MANAGED, KeyPairManagement.YBA_MANAGED],
      'SSH Keypair management choice is required.'
    )
  }),
  sshPrivateKeyContent: mixed().when(['editSSHKeypair', 'sshKeypairManagement'], {
    is: (editSSHKeypair, sshKeypairManagement) =>
      editSSHKeypair && sshKeypairManagement === KeyPairManagement.SELF_MANAGED,
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

const FORM_NAME = 'AZUProviderEditForm';

export const AZUProviderEditForm = ({
  editProvider,
  linkedUniverses,
  providerConfig
}: AZUProviderEditFormProps) => {
  const [isRegionFormModalOpen, setIsRegionFormModalOpen] = useState<boolean>(false);
  const [isDeleteRegionModalOpen, setIsDeleteRegionModalOpen] = useState<boolean>(false);
  const [regionSelection, setRegionSelection] = useState<CloudVendorRegionField>();
  const [regionOperation, setRegionOperation] = useState<RegionOperation>(RegionOperation.ADD);

  const { t } = useTranslation();
  const defaultValues = constructDefaultFormValues(providerConfig);
  const formMethods = useForm<AZUProviderEditFormFieldValues>({
    defaultValues: defaultValues,
    resolver: yupResolver(VALIDATION_SCHEMA)
  });

  const customerUUID = localStorage.getItem('customerId') ?? '';
  const customerRuntimeConfigQuery = useQuery(
    runtimeConfigQueryKey.customerScope(customerUUID),
    () => api.fetchRuntimeConfigs(customerUUID, true)
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
  if (customerRuntimeConfigQuery.isLoading || customerRuntimeConfigQuery.isIdle) {
    return <YBLoading />;
  }

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
    formMethods.reset(defaultValues);
  };
  const onFormSubmit: SubmitHandler<AZUProviderEditFormFieldValues> = async (formValues) => {
    if (formValues.ntpSetupType === NTPSetupType.SPECIFIED && !formValues.ntpServers.length) {
      formMethods.setError('ntpServers', {
        type: 'min',
        message: 'Please specify at least one NTP server.'
      });
      return;
    }

    try {
      const providerPayload = await constructProviderPayload(formValues, providerConfig);
      try {
        await editProvider(providerPayload);
      } catch (_) {
        // Handled with `mutateOptions.onError`
      }
    } catch (error: any) {
      toast.error(error.message ?? error);
    }
  };

  const regions = formMethods.watch('regions', defaultValues.regions);
  const setRegions = (regions: CloudVendorRegionField[]) =>
    formMethods.setValue('regions', regions, { shouldValidate: true });
  const onRegionFormSubmit = (currentRegion: CloudVendorRegionField) => {
    regionOperation === RegionOperation.ADD
      ? addItem(currentRegion, regions, setRegions)
      : editItem(currentRegion, regions, setRegions);
  };
  const onDeleteRegionSubmit = (currentRegion: CloudVendorRegionField) =>
    deleteItem(currentRegion, regions, setRegions);

  const currentProviderVersion = formMethods.watch('version', defaultValues.version);
  const keyPairManagement = formMethods.watch('sshKeypairManagement');
  const editSSHKeypair = formMethods.watch('editSSHKeypair', defaultValues.editSSHKeypair);
  const latestAccessKey = getLatestAccessKey(providerConfig.allAccessKeys);
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
        <FormContainer name="azuProviderForm" onSubmit={formMethods.handleSubmit(onFormSubmit)}>
          {currentProviderVersion < providerConfig.version && (
            <VersionWarningBanner onReset={onFormReset} dataTestIdPrefix={FORM_NAME} />
          )}
          <Typography variant="h3">Manage Azure Provider Configuration</Typography>
          <FormField providerNameField={true}>
            <FieldLabel>Provider Name</FieldLabel>
            <YBInputField
              control={formMethods.control}
              name="providerName"
              disabled={getIsFieldDisabled(
                ProviderCode.AZU,
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
                <FieldLabel>Client ID</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="azuClientId"
                  disabled={getIsFieldDisabled(
                    ProviderCode.AZU,
                    'azuClientId',
                    isFormDisabled,
                    isProviderInUse
                  )}
                  fullWidth
                />
              </FormField>
              <FormField>
                <FieldLabel>Client Secret</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="azuClientSecret"
                  disabled={getIsFieldDisabled(
                    ProviderCode.AZU,
                    'azuClientSecret',
                    isFormDisabled,
                    isProviderInUse
                  )}
                  fullWidth
                />
              </FormField>
              <FormField>
                <FieldLabel>Resource Group</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="azuRG"
                  disabled={getIsFieldDisabled(
                    ProviderCode.AZU,
                    'azuRG',
                    isFormDisabled,
                    isProviderInUse
                  )}
                  fullWidth
                />
              </FormField>
              <FormField>
                <FieldLabel>Network Resource Group</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="azuNetworkRG"
                  disabled={getIsFieldDisabled(
                    ProviderCode.AZU,
                    'azuNetworkRG',
                    isFormDisabled,
                    isProviderInUse
                  )}
                  fullWidth
                />
              </FormField>
              <FormField>
                <FieldLabel>Subscription ID</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="azuSubscriptionId"
                  disabled={getIsFieldDisabled(
                    ProviderCode.AZU,
                    'azuSubscriptionId',
                    isFormDisabled,
                    isProviderInUse
                  )}
                  fullWidth
                />
              </FormField>
              <FormField>
                <FieldLabel>Network Subscription ID</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="azuNetworkSubscriptionId"
                  disabled={getIsFieldDisabled(
                    ProviderCode.AZU,
                    'azuNetworkSubscriptionId',
                    isFormDisabled,
                    isProviderInUse
                  )}
                  fullWidth
                />
              </FormField>
              <FormField>
                <FieldLabel>Tenant ID</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="azuTenantId"
                  disabled={getIsFieldDisabled(
                    ProviderCode.AZU,
                    'azuTenantId',
                    isFormDisabled,
                    isProviderInUse
                  )}
                  fullWidth
                />
              </FormField>
              <FormField>
                <FieldLabel>Private DNS Zone (Optional)</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="azuHostedZoneId"
                  disabled={getIsFieldDisabled(
                    ProviderCode.AZU,
                    'azuHostedZoneId',
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
                      disabled={getIsFieldDisabled(
                        ProviderCode.AZU,
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
                providerCode={ProviderCode.AZU}
                providerOperation={ProviderOperation.EDIT}
                providerUuid={providerConfig.uuid}
                regions={regions}
                existingRegions={existingRegions}
                setRegionSelection={setRegionSelection}
                showAddRegionFormModal={showAddRegionFormModal}
                showEditRegionFormModal={showEditRegionFormModal}
                showDeleteRegionModal={showDeleteRegionModal}
                isDisabled={getIsFieldDisabled(
                  ProviderCode.AZU,
                  'regions',
                  isFormDisabled,
                  isProviderInUse
                )}
                isError={!!formMethods.formState.errors.regions}
                linkedUniverses={linkedUniverses}
                isEditInUseProviderEnabled={isEditInUseProviderEnabled}
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
              providerOperation={ProviderOperation.EDIT}
              providerStatus={providerConfig.usabilityState}
              linkedUniverses={linkedUniverses}
              isDisabled={getIsFieldDisabled(
                ProviderCode.AZU,
                'imageBundles',
                isFormDisabled,
                isProviderInUse
              )}
            />
            <FieldGroup heading="SSH Key Pairs">
              {sshConfigureMsg}
              <FormField>
                <FieldLabel>SSH User</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="sshUser"
                  disabled={
                    getIsFieldDisabled(
                      ProviderCode.AZU,
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
                      ProviderCode.AZU,
                      'sshPort',
                      isFormDisabled,
                      isProviderInUse
                    ) || isOsPatchingEnabled
                  }
                  fullWidth
                />
              </FormField>
              <FormField>
                <FieldLabel>Current SSH Keypair Name</FieldLabel>
                <YBInput value={latestAccessKey?.keyInfo?.keyPairName} disabled={true} fullWidth />
              </FormField>
              <FormField>
                <FieldLabel>Current SSH Private Key</FieldLabel>
                <YBInput value={latestAccessKey?.keyInfo?.privateKey} disabled={true} fullWidth />
              </FormField>
              <FormField>
                <FieldLabel>Change SSH Keypair</FieldLabel>
                <YBToggleField
                  name="editSSHKeypair"
                  control={formMethods.control}
                  disabled={getIsFieldDisabled(
                    ProviderCode.AZU,
                    'editSSHKeypair',
                    isFormDisabled,
                    isProviderInUse
                  )}
                />
              </FormField>
              {editSSHKeypair && (
                <>
                  <FormField>
                    <FieldLabel>Key Pair Management</FieldLabel>
                    <YBRadioGroupField
                      name="sshKeypairManagement"
                      control={formMethods.control}
                      options={KEY_PAIR_MANAGEMENT_OPTIONS}
                      orientation={RadioGroupOrientation.HORIZONTAL}
                      isDisabled={getIsFieldDisabled(
                        ProviderCode.AZU,
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
                            ProviderCode.AZU,
                            'sshKeypairName',
                            isFormDisabled,
                            isProviderInUse
                          )}
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
                          disabled={getIsFieldDisabled(
                            ProviderCode.AZU,
                            'sshPrivateKeyContent',
                            isFormDisabled,
                            isProviderInUse
                          )}
                        />
                      </FormField>
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
                    ProviderCode.AZU,
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
                    ProviderCode.AZU,
                    'ntpServers',
                    isFormDisabled,
                    isProviderInUse
                  )}
                  providerCode={ProviderCode.AZU}
                />
              </FormField>
            </FieldGroup>
            {(formMethods.formState.isValidating || formMethods.formState.isSubmitting) && (
              <Box display="flex" gridGap="5px" marginLeft="auto">
                <CircularProgress size={16} color="primary" thickness={5} />
              </Box>
            )}
          </Box>
          <Box marginTop="16px">
            <RbacValidator
              accessRequiredOn={ApiPermissionMap.MODIFY_PROVIDER}
              isControl
              overrideStyle={{ float: 'right' }}
            >
              <YBButton
                btnText="Apply Changes"
                btnClass="btn btn-default save-btn"
                btnType="submit"
                disabled={isFormDisabled || formMethods.formState.isValidating}
                data-testid={`${FORM_NAME}-SubmitButton`}
              />
            </RbacValidator>
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
      {/* Modals */}
      {isRegionFormModalOpen && (
        <ConfigureRegionModal
          configuredRegions={regions}
          isEditProvider={true}
          isProviderFormDisabled={getIsFieldDisabled(
            ProviderCode.AZU,
            'providerName',
            isFormDisabled,
            isProviderInUse
          )}
          inUseZones={inUseZones}
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

const constructDefaultFormValues = (
  providerConfig: AZUProvider
): Partial<AZUProviderEditFormFieldValues> => ({
  azuClientId: providerConfig.details.cloudInfo.azu.azuClientId ?? '',
  azuClientSecret: providerConfig.details.cloudInfo.azu.azuClientSecret ?? '',
  azuHostedZoneId: providerConfig.details.cloudInfo.azu.azuHostedZoneId ?? '',
  azuRG: providerConfig.details.cloudInfo.azu.azuRG ?? '',
  azuNetworkRG: providerConfig.details.cloudInfo.azu.azuNetworkRG ?? '',
  azuSubscriptionId: providerConfig.details.cloudInfo.azu.azuSubscriptionId ?? '',
  azuNetworkSubscriptionId: providerConfig.details.cloudInfo.azu.azuNetworkSubscriptionId ?? '',
  azuTenantId: providerConfig.details.cloudInfo.azu.azuTenantId ?? '',
  dbNodePublicInternetAccess: !providerConfig.details.airGapInstall,
  editSSHKeypair: false,
  ntpServers: providerConfig.details.ntpServers,
  ntpSetupType: getNtpSetupType(providerConfig),
  providerName: providerConfig.name,
  imageBundles: providerConfig.imageBundles,
  regions: providerConfig.regions.map((region) => ({
    fieldId: generateLowerCaseAlphanumericId(),
    code: region.code,
    name: region.name,
    vnet: region.details.cloudInfo.azu.vnet,
    securityGroupId: region.details.cloudInfo.azu.securityGroupId,
    ybImage: region.details.cloudInfo.azu.ybImage ?? '',
    zones: region.zones
  })),
  sshKeypairManagement: getLatestAccessKey(providerConfig.allAccessKeys)?.keyInfo.managementState,
  sshPort: providerConfig.details.sshPort ?? null,
  sshUser: providerConfig.details.sshUser ?? '',
  version: providerConfig.version
});

const constructProviderPayload = async (
  formValues: AZUProviderEditFormFieldValues,
  providerConfig: AZUProvider
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

  const allAccessKeysPayload = constructAccessKeysEditPayload(
    formValues.editSSHKeypair,
    formValues.sshKeypairManagement,
    { sshKeypairName: formValues.sshKeypairName, sshPrivateKeyContent: sshPrivateKeyContent },
    providerConfig.allAccessKeys
  );

  const {
    airGapInstall,
    cloudInfo,
    ntpServers,
    setUpChrony,
    sshPort,
    sshUser,
    ...unexposedProviderDetailFields
  } = providerConfig.details;
  return {
    code: ProviderCode.AZU,
    name: formValues.providerName,
    ...allAccessKeysPayload,
    details: {
      ...unexposedProviderDetailFields,
      airGapInstall: !formValues.dbNodePublicInternetAccess,
      cloudInfo: {
        [ProviderCode.AZU]: {
          azuClientId: formValues.azuClientId,
          azuClientSecret: formValues.azuClientSecret,
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
    imageBundles,
    regions: [
      ...formValues.regions.map<AZURegionMutation>((regionFormValues) => {
        const existingRegion = findExistingRegion<AZUProvider, AZURegion>(
          providerConfig,
          regionFormValues.code
        );
        return {
          ...existingRegion,
          code: regionFormValues.code,
          details: {
            ...existingRegion?.details,
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
          zones: [
            ...regionFormValues.zones.map<AZUAvailabilityZoneMutation>((azFormValues) => {
              const existingZone = findExistingZone<AZURegion, AZUAvailabilityZone>(
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
      ...getDeletedRegions<AZURegion, CloudVendorRegionField>(
        providerConfig.regions,
        formValues.regions
      )
    ],
    version: formValues.version
  };
};
