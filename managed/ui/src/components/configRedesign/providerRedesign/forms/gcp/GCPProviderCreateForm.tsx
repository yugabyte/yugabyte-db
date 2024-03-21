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
  DEFAULT_SSH_PORT,
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
  constructAccessKeysCreatePayload,
  deleteItem,
  editItem,
  generateLowerCaseAlphanumericId,
  getIsFormDisabled,
  readFileAsText
} from '../utils';
import { FormContainer } from '../components/FormContainer';
import { ACCEPTABLE_CHARS } from '../../../../config/constants';
import { FormField } from '../components/FormField';
import { FieldLabel } from '../components/FieldLabel';
import { CreateInfraProvider } from '../../InfraProvider';
import { YBErrorIndicator, YBLoading } from '../../../../common/indicators';
import { api, hostInfoQueryKey } from '../../../../../redesign/helpers/api';
import { getYBAHost } from '../../utils';
import { YBAHost } from '../../../../../redesign/helpers/constants';
import { RegionOperation } from '../configureRegion/constants';
import { toast } from 'react-toastify';
import { assertUnreachableCase } from '../../../../../utils/errorHandlingUtils';
import { NTP_SERVER_REGEX } from '../constants';

import { GCPRegionMutation, GCPAvailabilityZoneMutation, YBProviderMutation } from '../../types';
import { RbacValidator } from '../../../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ConfigureSSHDetailsMsg, IsOsPatchingEnabled, constructImageBundlePayload } from '../../components/linuxVersionCatalog/LinuxVersionUtils';
import { ApiPermissionMap } from '../../../../../redesign/features/rbac/ApiAndUserPermMapping';
import { LinuxVersionCatalog } from '../../components/linuxVersionCatalog/LinuxVersionCatalog';
import { CloudType } from '../../../../../redesign/helpers/dtos';
import { ImageBundle } from '../../../../../redesign/features/universe/universe-form/utils/dto';

interface GCPProviderCreateFormProps {
  createInfraProvider: CreateInfraProvider;
  onBack: () => void;
}

interface GCPProviderCreateFormFieldValues {
  dbNodePublicInternetAccess: boolean;
  destVpcId: string;
  gceProject: string;
  googleServiceAccount: File;
  ntpServers: string[];
  ntpSetupType: NTPSetupType;
  providerCredentialType: ProviderCredentialType;
  providerName: string;
  regions: CloudVendorRegionField[];
  sharedVPCProject: string;
  sshKeypairManagement: KeyPairManagement;
  sshKeypairName: string;
  sshPort: number;
  sshPrivateKeyContent: File;
  sshUser: string;
  vpcSetupType: VPCSetupType;
  ybFirewallTags: string;
  imageBundles: ImageBundle[];
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
  // Specified provider credential types
  googleServiceAccount: mixed().when('providerCredentialType', {
    is: ProviderCredentialType.SPECIFIED_SERVICE_ACCOUNT,
    then: mixed().required('Service account config is required.')
  }),
  destVpcId: string().when('vpcSetupType', {
    is: (vpcSetupType: VPCSetupType) =>
      ([VPCSetupType.EXISTING, VPCSetupType.NEW] as VPCSetupType[]).includes(vpcSetupType),
    then: string().required('Custom GCE Network is required.')
  }),
  sshPrivateKeyContent: mixed().when('sshKeypairManagement', {
    is: KeyPairManagement.SELF_MANAGED,
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

const FORM_NAME = 'GCPProviderCreateForm';

export const GCPProviderCreateForm = ({
  onBack,
  createInfraProvider
}: GCPProviderCreateFormProps) => {
  const [isRegionFormModalOpen, setIsRegionFormModalOpen] = useState<boolean>(false);
  const [isDeleteRegionModalOpen, setIsDeleteRegionModalOpen] = useState<boolean>(false);
  const [regionSelection, setRegionSelection] = useState<CloudVendorRegionField>();
  const [regionOperation, setRegionOperation] = useState<RegionOperation>(RegionOperation.ADD);
  const { t } = useTranslation();

  const defaultValues: Partial<GCPProviderCreateFormFieldValues> = {
    dbNodePublicInternetAccess: true,
    ntpServers: [] as string[],
    ntpSetupType: NTPSetupType.CLOUD_VENDOR,
    providerCredentialType: ProviderCredentialType.SPECIFIED_SERVICE_ACCOUNT,
    providerName: '',
    regions: [] as CloudVendorRegionField[],
    sshKeypairManagement: KeyPairManagement.YBA_MANAGED,
    sshPort: DEFAULT_SSH_PORT,
    vpcSetupType: VPCSetupType.EXISTING
  } as const;
  const formMethods = useForm<GCPProviderCreateFormFieldValues>({
    defaultValues: defaultValues,
    resolver: yupResolver(VALIDATION_SCHEMA)
  });

  const hostInfoQuery = useQuery(hostInfoQueryKey.ALL, () => api.fetchHostInfo());

  const isOsPatchingEnabled = IsOsPatchingEnabled();
  const sshConfigureMsg = ConfigureSSHDetailsMsg();


  if (hostInfoQuery.isLoading || hostInfoQuery.isIdle) {
    return <YBLoading />;
  }
  if (hostInfoQuery.isError) {
    return (
      <YBErrorIndicator
        customErrorMessage={t('failedToFetchHostInfo', { keyPrefix: 'queryError' })}
      />
    );
  }

  const onFormSubmit: SubmitHandler<GCPProviderCreateFormFieldValues> = async (formValues) => {
    if (formValues.ntpSetupType === NTPSetupType.SPECIFIED && !formValues.ntpServers.length) {
      formMethods.setError('ntpServers', {
        type: 'min',
        message: 'Please specify at least one NTP server.'
      });
      return;
    }

    let googleServiceAccount = null;
    if (
      formValues.providerCredentialType === ProviderCredentialType.SPECIFIED_SERVICE_ACCOUNT &&
      formValues.googleServiceAccount
    ) {
      const googleServiceAccountText = await readFileAsText(formValues.googleServiceAccount);
      if (googleServiceAccountText) {
        try {
          googleServiceAccount = JSON.parse(googleServiceAccountText);
        } catch (error) {
          toast.error(`An error occured while parsing the service account JSON: ${error}`);
          return;
        }
      }
    }

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
    //       backward compatability reasons.
    const vpcConfig =
      formValues.vpcSetupType === VPCSetupType.HOST_INSTANCE
        ? {
          useHostVPC: true
        }
        : formValues.vpcSetupType === VPCSetupType.EXISTING
          ? {
            useHostVPC: true,
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

    const allAccessKeysPayload = constructAccessKeysCreatePayload(
      formValues.sshKeypairManagement,
      formValues.sshKeypairName,
      sshPrivateKeyContent
    );

    const imageBundles = constructImageBundlePayload(formValues);

    const providerPayload: YBProviderMutation = {
      code: ProviderCode.GCP,
      name: formValues.providerName,
      ...allAccessKeysPayload,
      details: {
        airGapInstall: !formValues.dbNodePublicInternetAccess,
        cloudInfo: {
          [ProviderCode.GCP]: {
            ...vpcConfig,
            ...gcpCredentials,
            ...(formValues.sharedVPCProject && { sharedVPCProject: formValues.sharedVPCProject }),
            ...(formValues.ybFirewallTags && { ybFirewallTags: formValues.ybFirewallTags })
          }
        },
        ntpServers: formValues.ntpServers,
        setUpChrony: formValues.ntpSetupType !== NTPSetupType.NO_NTP,
        ...(formValues.sshPort && { sshPort: formValues.sshPort }),
        ...(formValues.sshUser && { sshUser: formValues.sshUser })
      },
      regions: formValues.regions.map<GCPRegionMutation>((regionFormValues) => ({
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
        zones: regionFormValues.zones.map<GCPAvailabilityZoneMutation>((zone) => ({
          code: zone.code,
          name: zone.code,
          subnet: regionFormValues.sharedSubnet ?? ''
        }))
      })),
      imageBundles
    };
    try {
      await createInfraProvider(providerPayload);
    } catch (_) {
      // Request errors are handled by the onError callback
    }
  };

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
  const keyPairManagement = formMethods.watch(
    'sshKeypairManagement',
    defaultValues.sshKeypairManagement
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
  const vpcSetupType = formMethods.watch('vpcSetupType', defaultValues.vpcSetupType);
  const isFormDisabled = getIsFormDisabled(formMethods.formState);
  return (
    <Box display="flex" justifyContent="center">
      <FormProvider {...formMethods}>
        <FormContainer name={FORM_NAME} onSubmit={formMethods.handleSubmit(onFormSubmit)}>
          <Typography variant="h3">Create GCP Provider Configuration</Typography>
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
                <FieldLabel>Credential Type</FieldLabel>
                <YBRadioGroupField
                  name="providerCredentialType"
                  control={formMethods.control}
                  options={credentialOptions}
                  orientation={RadioGroupOrientation.HORIZONTAL}
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
                    disabled={isFormDisabled}
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
                  disabled={isFormDisabled}
                  fullWidth
                />
              </FormField>
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
                />
              </FormField>
              {(vpcSetupType === VPCSetupType.EXISTING || vpcSetupType === VPCSetupType.NEW) && (
                <FormField>
                  <FieldLabel>Custom GCE Network Name</FieldLabel>
                  <YBInputField
                    control={formMethods.control}
                    name="destVpcId"
                    disabled={isFormDisabled}
                    fullWidth
                  />
                </FormField>
              )}
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
                providerCode={ProviderCode.GCP}
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
            <LinuxVersionCatalog
              control={formMethods.control as any}
              providerType={CloudType.gcp}
              viewMode="CREATE"
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
                <FieldLabel>Firewall Tags</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="ybFirewallTags"
                  placeholder="my-firewall-tag-1,my-firewall-tag-2"
                  disabled={isFormDisabled}
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
                  disabled={isFormDisabled}
                />
              </FormField>
              <FormField>
                <FieldLabel>NTP Setup</FieldLabel>
                <NTPConfigField isDisabled={isFormDisabled} providerCode={ProviderCode.GCP} />
              </FormField>
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
        <ConfigureRegionModal
          configuredRegions={regions}
          isEditProvider={false}
          isProviderFormDisabled={isFormDisabled}
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
