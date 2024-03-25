import { useState } from 'react';
import { FormProvider, SubmitHandler, useForm } from 'react-hook-form';
import { Box, CircularProgress, FormHelperText, Typography } from '@material-ui/core';
import { yupResolver } from '@hookform/resolvers/yup';
import { useQuery } from 'react-query';
import { array, mixed, object, string } from 'yup';
import { useTranslation } from 'react-i18next';

import {
  OptionProps,
  RadioGroupOrientation,
  YBInput,
  YBInputField,
  YBRadioGroupField,
  YBToggleField
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
  KeyPairManagement,
  KEY_PAIR_MANAGEMENT_OPTIONS,
  NTPSetupType,
  ProviderCode,
  VPCSetupType,
  VPCSetupTypeLabel
} from '../../constants';
import { FieldGroup } from '../components/FieldGroup';
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
import { FormContainer } from '../components/FormContainer';
import { ACCEPTABLE_CHARS } from '../../../../config/constants';
import { FormField } from '../components/FormField';
import { FieldLabel } from '../components/FieldLabel';
import { YBErrorIndicator, YBLoading } from '../../../../common/indicators';
import { api, hostInfoQueryKey, runtimeConfigQueryKey } from '../../../../../redesign/helpers/api';
import {
  findExistingRegion,
  getDeletedRegions,
  getInUseAzs,
  getLatestAccessKey,
  getNtpSetupType,
  getYBAHost
} from '../../utils';
import { RuntimeConfigKey, YBAHost } from '../../../../../redesign/helpers/constants';
import { RegionOperation } from '../configureRegion/constants';
import { toast } from 'react-toastify';
import { assertUnreachableCase } from '../../../../../utils/errorHandlingUtils';
import { EditProvider } from '../ProviderEditView';
import { VersionWarningBanner } from '../components/VersionWarningBanner';
import { NTP_SERVER_REGEX } from '../constants';
import { UniverseItem } from '../../providerView/providerDetails/UniverseTable';

import {
  GCPRegionMutation,
  GCPAvailabilityZoneMutation,
  YBProviderMutation,
  GCPProvider,
  GCPRegion,
  ImageBundle
} from '../../types';
import {
  hasNecessaryPerm,
  RbacValidator
} from '../../../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ConfigureSSHDetailsMsg, IsOsPatchingEnabled, constructImageBundlePayload } from '../../components/linuxVersionCatalog/LinuxVersionUtils';
import { ApiPermissionMap } from '../../../../../redesign/features/rbac/ApiAndUserPermMapping';
import { LinuxVersionCatalog } from '../../components/linuxVersionCatalog/LinuxVersionCatalog';
import { CloudType } from '../../../../../redesign/helpers/dtos';

interface GCPProviderEditFormProps {
  editProvider: EditProvider;
  linkedUniverses: UniverseItem[];
  providerConfig: GCPProvider;
}

export interface GCPProviderEditFormFieldValues {
  dbNodePublicInternetAccess: boolean;
  destVpcId: string;
  editCloudCredentials: boolean;
  editSSHKeypair: boolean;
  gceProject: string;
  googleServiceAccount: File;
  ntpServers: string[];
  ntpSetupType: NTPSetupType;
  providerCredentialType: ProviderCredentialType;
  providerName: string;
  imageBundles: ImageBundle[];
  regions: CloudVendorRegionField[];
  sharedVPCProject: string;
  sshKeypairManagement: KeyPairManagement;
  sshKeypairName: string;
  sshPort: number | null;
  sshPrivateKeyContent: File;
  sshUser: string;
  version: number;
  vpcSetupType: VPCSetupType;
  ybFirewallTags: string;
}

const ProviderCredentialType = {
  HOST_INSTANCE_SERVICE_ACCOUNT: 'hostInstanceServiceAccount',
  SPECIFIED_SERVICE_ACCOUNT: 'specifiedServiceAccount'
} as const;
type ProviderCredentialType = typeof ProviderCredentialType[keyof typeof ProviderCredentialType];

const YB_VPC_NAME_BASE = 'yb-gcp-network';

const VALIDATION_SCHEMA = object().shape({
  providerName: string()
    .required('Provider Name is required.')
    .matches(
      ACCEPTABLE_CHARS,
      'Provider name cannot contain special characters other than "-", and "_"'
    ),
  googleServiceAccount: mixed().when(['editCloudCredentials', 'providerCredentialType'], {
    is: (editCloudCredentials, providerCredentialType) =>
      editCloudCredentials &&
      providerCredentialType === ProviderCredentialType.SPECIFIED_SERVICE_ACCOUNT,
    then: mixed().required('Service account config is required.')
  }),
  destVpcId: string().when('vpcSetupType', {
    is: (vpcSetupType: VPCSetupType) =>
      ([VPCSetupType.EXISTING, VPCSetupType.NEW] as VPCSetupType[]).includes(vpcSetupType),
    then: string().required('Custom GCE Network is required.')
  }),
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

const FORM_NAME = 'GCPProviderEditForm';

export const GCPProviderEditForm = ({
  editProvider,
  linkedUniverses,
  providerConfig
}: GCPProviderEditFormProps) => {
  const [isRegionFormModalOpen, setIsRegionFormModalOpen] = useState<boolean>(false);
  const [isDeleteRegionModalOpen, setIsDeleteRegionModalOpen] = useState<boolean>(false);
  const [regionSelection, setRegionSelection] = useState<CloudVendorRegionField>();
  const [regionOperation, setRegionOperation] = useState<RegionOperation>(RegionOperation.ADD);
  const { t } = useTranslation();

  const defaultValues = constructDefaultFormValues(providerConfig);
  const formMethods = useForm<GCPProviderEditFormFieldValues>({
    defaultValues: defaultValues,
    resolver: yupResolver(VALIDATION_SCHEMA)
  });

  const customerUUID = localStorage.getItem('customerId') ?? '';
  const customerRuntimeConfigQuery = useQuery(
    runtimeConfigQueryKey.customerScope(customerUUID),
    () => api.fetchRuntimeConfigs(customerUUID, true)
  );
  const hostInfoQuery = useQuery(hostInfoQueryKey.ALL, () => api.fetchHostInfo());

  const isOsPatchingEnabled = IsOsPatchingEnabled();
  const sshConfigureMsg = ConfigureSSHDetailsMsg();

  if (hostInfoQuery.isError) {
    return (
      <YBErrorIndicator
        customErrorMessage={t('failedToFetchHostInfo', { keyPrefix: 'queryError' })}
      />
    );
  }
  if (customerRuntimeConfigQuery.isError) {
    return (
      <YBErrorIndicator
        customErrorMessage={t('failedToFetchCustomerRuntimeConfig', { keyPrefix: 'queryError' })}
      />
    );
  }
  if (
    hostInfoQuery.isLoading ||
    hostInfoQuery.isIdle ||
    customerRuntimeConfigQuery.isLoading ||
    customerRuntimeConfigQuery.isIdle
  ) {
    return <YBLoading />;
  }

  const onFormSubmit: SubmitHandler<GCPProviderEditFormFieldValues> = async (formValues) => {
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

  const credentialOptions: OptionProps[] = [
    {
      value: ProviderCredentialType.SPECIFIED_SERVICE_ACCOUNT,
      label: 'Upload service account config'
    },
    {
      value: ProviderCredentialType.HOST_INSTANCE_SERVICE_ACCOUNT,
      label: `Use service account from this YBA host's instance`,
      disabled: getYBAHost(hostInfoQuery.data) !== YBAHost.GCP
    }
  ];

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

  const providerCredentialType = formMethods.watch(
    'providerCredentialType',
    defaultValues.providerCredentialType
  );

  const vpcSetupOptions: OptionProps[] = [
    {
      value: VPCSetupType.EXISTING,
      label: VPCSetupTypeLabel[VPCSetupType.EXISTING]
    },
    {
      value: VPCSetupType.HOST_INSTANCE,
      label: VPCSetupTypeLabel[VPCSetupType.HOST_INSTANCE],
      disabled: providerCredentialType !== ProviderCredentialType.HOST_INSTANCE_SERVICE_ACCOUNT
    },
    {
      value: VPCSetupType.NEW,
      label: VPCSetupTypeLabel[VPCSetupType.NEW],
      disabled: true // Disabling 'Create new VPC' until we're able to fully test our support for this.
    }
  ];

  const currentProviderVersion = formMethods.watch('version', defaultValues.version);
  const vpcSetupType = formMethods.watch('vpcSetupType', defaultValues.vpcSetupType);
  const keyPairManagement = formMethods.watch('sshKeypairManagement');
  const editSSHKeypair = formMethods.watch('editSSHKeypair', defaultValues.editSSHKeypair);
  const editCloudCredentials = formMethods.watch(
    'editCloudCredentials',
    defaultValues.editCloudCredentials
  );
  const serviceAccountIdentifer = providerConfig.details.cloudInfo.gcp.useHostCredentials
    ? 'YBA Host Instance'
    : providerConfig.details.cloudInfo.gcp.gceApplicationCredentials?.client_email;
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
        <FormContainer name="gcpProviderForm" onSubmit={formMethods.handleSubmit(onFormSubmit)}>
          {currentProviderVersion < providerConfig.version && (
            <VersionWarningBanner onReset={onFormReset} dataTestIdPrefix={FORM_NAME} />
          )}
          <Typography variant="h3">Manage GCP Provider Configuration</Typography>
          <FormField providerNameField={true}>
            <FieldLabel>Provider Name</FieldLabel>
            <YBInputField
              control={formMethods.control}
              name="providerName"
              disabled={getIsFieldDisabled(
                ProviderCode.GCP,
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
                <FieldLabel>Current Service Account</FieldLabel>
                <YBInput value={serviceAccountIdentifer} disabled={true} fullWidth />
              </FormField>
              <FormField>
                <FieldLabel>Current GCE Project Name</FieldLabel>
                <YBInput
                  value={providerConfig.details.cloudInfo.gcp.gceProject}
                  disabled={true}
                  fullWidth
                />
              </FormField>
              {!!providerConfig.details.cloudInfo.gcp?.sharedVPCProject && (
                <FormField>
                  <FieldLabel>Current Shared VPC Project</FieldLabel>
                  <YBInput
                    value={providerConfig.details.cloudInfo.gcp.sharedVPCProject}
                    disabled={true}
                    fullWidth
                  />
                </FormField>
              )}
              <FormField>
                <FieldLabel>Change Cloud Credentials</FieldLabel>
                <YBToggleField
                  name="editCloudCredentials"
                  control={formMethods.control}
                  disabled={getIsFieldDisabled(
                    ProviderCode.GCP,
                    'editCloudCredentials',
                    isFormDisabled,
                    isProviderInUse
                  )}
                />
              </FormField>
              {editCloudCredentials && (
                <>
                  <FormField>
                    <FieldLabel>Credential Type</FieldLabel>
                    <YBRadioGroupField
                      name="providerCredentialType"
                      control={formMethods.control}
                      options={credentialOptions}
                      orientation={RadioGroupOrientation.HORIZONTAL}
                      isDisabled={getIsFieldDisabled(
                        ProviderCode.GCP,
                        'providerCredentialType',
                        isFormDisabled,
                        isProviderInUse
                      )}
                    />
                  </FormField>
                  {providerCredentialType === ProviderCredentialType.SPECIFIED_SERVICE_ACCOUNT && (
                    <FormField>
                      <FieldLabel>Service Account</FieldLabel>
                      <YBDropZoneField
                        name="googleServiceAccount"
                        control={formMethods.control}
                        actionButtonText="Upload Google service account JSON"
                        multipleFiles={false}
                        showHelpText={false}
                        disabled={getIsFieldDisabled(
                          ProviderCode.GCP,
                          'googleServiceAccount',
                          isFormDisabled,
                          isProviderInUse
                        )}
                      />
                    </FormField>
                  )}
                  <FormField>
                    <FieldLabel
                      infoTitle="Shared VPC Project"
                      infoContent="If you want to use Shared VPC to connect resources from multiple projects to a common VPC, you can specify the project for the same here."
                    >
                      Shared VPC Project (Optional)
                    </FieldLabel>
                    <YBInputField
                      control={formMethods.control}
                      name="sharedVPCProject"
                      disabled={getIsFieldDisabled(
                        ProviderCode.GCP,
                        'sharedVPCProject',
                        isFormDisabled,
                        isProviderInUse
                      )}
                      fullWidth
                    />
                  </FormField>
                </>
              )}
              <FormField>
                <FieldLabel>VPC Setup</FieldLabel>
                <YBRadioGroupField
                  name="vpcSetupType"
                  control={formMethods.control}
                  options={vpcSetupOptions}
                  orientation={RadioGroupOrientation.HORIZONTAL}
                  onRadioChange={(_event, value) => {
                    if (value === VPCSetupType.NEW) {
                      formMethods.setValue(
                        'destVpcId',
                        `${YB_VPC_NAME_BASE}-${generateLowerCaseAlphanumericId()}`
                      );
                    } else {
                      formMethods.setValue('destVpcId', '');
                    }
                  }}
                  isDisabled={getIsFieldDisabled(
                    ProviderCode.GCP,
                    'vpcSetupType',
                    isFormDisabled,
                    isProviderInUse
                  )}
                />
              </FormField>
              {(vpcSetupType === VPCSetupType.EXISTING || vpcSetupType === VPCSetupType.NEW) && (
                <FormField>
                  <FieldLabel>Custom GCE Network Name</FieldLabel>
                  <YBInputField
                    control={formMethods.control}
                    name="destVpcId"
                    disabled={getIsFieldDisabled(
                      ProviderCode.GCP,
                      'destVpcId',
                      isFormDisabled,
                      isProviderInUse
                    )}
                    fullWidth
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
                        ProviderCode.GCP,
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
                providerCode={ProviderCode.GCP}
                providerUuid={providerConfig.uuid}
                regions={regions}
                existingRegions={existingRegions}
                setRegionSelection={setRegionSelection}
                showAddRegionFormModal={showAddRegionFormModal}
                showEditRegionFormModal={showEditRegionFormModal}
                showDeleteRegionModal={showDeleteRegionModal}
                disabled={getIsFieldDisabled(
                  ProviderCode.GCP,
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
              providerType={CloudType.gcp}
              viewMode="EDIT"
              providerStatus={providerConfig.usabilityState}
            />
            <FieldGroup heading="SSH Key Pairs">
              {sshConfigureMsg}
              <FormField>
                <FieldLabel>SSH User</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="sshUser"
                  disabled={getIsFieldDisabled(
                    ProviderCode.GCP,
                    'sshUser',
                    isFormDisabled,
                    isProviderInUse
                  ) || isOsPatchingEnabled}
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
                  disabled={getIsFieldDisabled(
                    ProviderCode.GCP,
                    'sshPort',
                    isFormDisabled,
                    isProviderInUse
                  ) || isOsPatchingEnabled}
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
                    ProviderCode.GCP,
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
                        ProviderCode.GCP,
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
                            ProviderCode.GCP,
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
                            ProviderCode.GCP,
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
                <FieldLabel>Firewall Tags</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="ybFirewallTags"
                  placeholder="my-firewall-tag-1,my-firewall-tag-2"
                  disabled={getIsFieldDisabled(
                    ProviderCode.GCP,
                    'ybFirewallTags',
                    isFormDisabled,
                    isProviderInUse
                  )}
                  fullWidth
                />
              </FormField>
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
                    ProviderCode.GCP,
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
                    ProviderCode.GCP,
                    'ntpServers',
                    isFormDisabled,
                    isProviderInUse
                  )}
                  providerCode={ProviderCode.GCP}
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
      {isRegionFormModalOpen && (
        <ConfigureRegionModal
          configuredRegions={regions}
          isEditProvider={true}
          isProviderFormDisabled={getIsFieldDisabled(
            ProviderCode.GCP,
            'regions',
            isFormDisabled,
            isProviderInUse
          )}
          inUseZones={inUseZones}
          onClose={hideRegionFormModal}
          onRegionSubmit={onRegionFormSubmit}
          open={isRegionFormModalOpen}
          providerCode={ProviderCode.GCP}
          regionOperation={regionOperation}
          regionSelection={regionSelection}
          vpcSetupType={vpcSetupType}
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
  providerConfig: GCPProvider
): Partial<GCPProviderEditFormFieldValues> => ({
  dbNodePublicInternetAccess: !providerConfig.details.airGapInstall,
  destVpcId: providerConfig.details.cloudInfo.gcp.destVpcId ?? '',
  editCloudCredentials: false,
  editSSHKeypair: false,
  ntpServers: providerConfig.details.ntpServers,
  ntpSetupType: getNtpSetupType(providerConfig),
  providerName: providerConfig.name,
  providerCredentialType: providerConfig.details.cloudInfo.gcp.useHostCredentials
    ? ProviderCredentialType.HOST_INSTANCE_SERVICE_ACCOUNT
    : ProviderCredentialType.SPECIFIED_SERVICE_ACCOUNT,
  imageBundles: providerConfig.imageBundles,
  regions: providerConfig.regions.map((region) => ({
    code: region.code,
    fieldId: generateLowerCaseAlphanumericId(),
    instanceTemplate: region.details.cloudInfo.gcp.instanceTemplate ?? '',
    name: region.name,
    sharedSubnet: region.zones?.[0]?.subnet ?? '',
    ybImage: region.details.cloudInfo.gcp.ybImage ?? '',
    zones: region.zones.map<GCPAvailabilityZoneMutation>((zone) => ({
      code: zone.code,
      name: zone.code,
      subnet: zone.subnet
    }))
  })),
  sshKeypairManagement: getLatestAccessKey(providerConfig.allAccessKeys)?.keyInfo.managementState,
  sshPort: providerConfig.details.sshPort ?? null,
  sshUser: providerConfig.details.sshUser ?? '',
  version: providerConfig.version,
  vpcSetupType: providerConfig.details.cloudInfo.gcp.vpcType,
  ybFirewallTags: providerConfig.details.cloudInfo.gcp.ybFirewallTags ?? ''
});

const constructProviderPayload = async (
  formValues: GCPProviderEditFormFieldValues,
  providerConfig: GCPProvider
): Promise<YBProviderMutation> => {
  let googleServiceAccount = null;
  if (
    formValues.providerCredentialType === ProviderCredentialType.SPECIFIED_SERVICE_ACCOUNT &&
    formValues.googleServiceAccount
  ) {
    try {
      const googleServiceAccountText = await readFileAsText(formValues.googleServiceAccount);
      if (googleServiceAccountText) {
        googleServiceAccount = JSON.parse(googleServiceAccountText);
      }
    } catch (error) {
      throw new Error(
        `An error occurred while processing the Google service account file: ${error}`
      );
    }
  }

  const imageBundles = constructImageBundlePayload(formValues);

  let sshPrivateKeyContent = '';
  try {
    sshPrivateKeyContent =
      formValues.sshKeypairManagement === KeyPairManagement.SELF_MANAGED &&
        formValues.sshPrivateKeyContent
        ? (await readFileAsText(formValues.sshPrivateKeyContent)) ?? ''
        : '';
  } catch (error) {
    throw new Error(`An error occurred while processing the SSH private key file: ${error}`);
  }

  // Note: Backend expects `useHostVPC` to be true for both host instance VPC and specified VPC for
  //       backwards compatability reasons.
  const vpcConfig =
    formValues.vpcSetupType === VPCSetupType.HOST_INSTANCE
      ? {
        useHostVPC: true
      }
      : formValues.vpcSetupType === VPCSetupType.EXISTING
        ? {
          useHostVPC: true, // Must be sent as true for backwards compatability.
          destVpcId: formValues.destVpcId
        }
        : formValues.vpcSetupType === VPCSetupType.NEW
          ? {
            useHostVPC: false,
            destVpcId: formValues.destVpcId
          }
          : assertUnreachableCase(formValues.vpcSetupType);

  const gcpCredentials =
    formValues.providerCredentialType === ProviderCredentialType.HOST_INSTANCE_SERVICE_ACCOUNT
      ? {
        useHostCredentials: true
      }
      : formValues.providerCredentialType === ProviderCredentialType.SPECIFIED_SERVICE_ACCOUNT
        ? {
          gceApplicationCredentials: googleServiceAccount,
          gceProject: googleServiceAccount?.project_id ?? '',
          useHostCredentials: false
        }
        : assertUnreachableCase(formValues.providerCredentialType);

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
    code: ProviderCode.GCP,
    name: formValues.providerName,
    ...allAccessKeysPayload,
    details: {
      ...unexposedProviderDetailFields,
      airGapInstall: !formValues.dbNodePublicInternetAccess,
      cloudInfo: {
        [ProviderCode.GCP]: {
          ...vpcConfig,
          ...(formValues.editCloudCredentials
            ? {
              ...gcpCredentials,
              ...(formValues.sharedVPCProject && {
                sharedVPCProject: formValues.sharedVPCProject
              })
            }
            : {
              useHostCredentials: cloudInfo.gcp.useHostCredentials,
              gceProject: cloudInfo.gcp.gceProject,
              gceApplicationCredentials: cloudInfo.gcp.gceApplicationCredentials,
              gceApplicationCredentialsPath: cloudInfo.gcp.gceApplicationCredentialsPath,
              ...(cloudInfo.gcp.sharedVPCProject && {
                sharedVPCProject: cloudInfo.gcp.sharedVPCProject
              })
            }),
          ...(formValues.ybFirewallTags && { ybFirewallTags: formValues.ybFirewallTags })
        }
      },
      ntpServers: formValues.ntpServers,
      setUpChrony: formValues.ntpSetupType !== NTPSetupType.NO_NTP,
      ...(formValues.sshPort && { sshPort: formValues.sshPort }),
      ...(formValues.sshUser && { sshUser: formValues.sshUser })
    },
    imageBundles,
    regions: [
      ...formValues.regions.map<GCPRegionMutation>((regionFormValues) => {
        const existingRegion = findExistingRegion<GCPProvider, GCPRegion>(
          providerConfig,
          regionFormValues.code
        );
        return {
          ...(existingRegion && {
            active: existingRegion.active,
            uuid: existingRegion.uuid
          }),
          code: regionFormValues.code,
          details: {
            cloudInfo: {
              [ProviderCode.GCP]: {
                ...(regionFormValues.ybImage && { ybImage: regionFormValues.ybImage }),
                ...(regionFormValues.instanceTemplate && {
                  instanceTemplate: regionFormValues.instanceTemplate
                })
              }
            }
          },
          zones: existingRegion
            ? existingRegion.zones.map((zone) => ({
              active: zone.active,
              code: zone.code,
              name: zone.name,
              subnet: regionFormValues.sharedSubnet ?? '',
              uuid: zone.uuid
            }))
            : regionFormValues.zones.map<GCPAvailabilityZoneMutation>((zone) => ({
              code: zone.code,
              name: zone.code,
              subnet: regionFormValues.sharedSubnet ?? ''
            }))
        };
      }),
      ...getDeletedRegions(providerConfig.regions, formValues.regions)
    ],
    version: formValues.version
  };
};
