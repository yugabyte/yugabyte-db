import React, { useState } from 'react';
import { FormProvider, SubmitHandler, useForm } from 'react-hook-form';
import { Box, FormHelperText, Typography } from '@material-ui/core';
import { yupResolver } from '@hookform/resolvers/yup';
import { useQuery } from 'react-query';
import { array, mixed, object, string } from 'yup';

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
  ASYNC_ERROR,
  DEFAULT_SSH_PORT,
  NTPSetupType,
  ProviderCode,
  VPCSetupType,
  VPCSetupTypeLabel
} from '../../constants';
import { FieldGroup } from '../components/FieldGroup';
import {
  addItem,
  deleteItem,
  editItem,
  handleFormServerError,
  generateLowerCaseAlphanumericId,
  readFileAsText
} from '../utils';
import { FormContainer } from '../components/FormContainer';
import { ACCEPTABLE_CHARS } from '../../../../config/constants';
import { FormField } from '../components/FormField';
import { FieldLabel } from '../components/FieldLabel';
import { CreateInfraProvider } from '../../InfraProvider';
import { GCP_REGIONS } from '../../providerRegionsData';
import { YBErrorIndicator, YBLoading } from '../../../../common/indicators';
import { api, hostInfoQueryKey } from '../../../../../redesign/helpers/api';
import { getYBAHost } from '../../utils';
import { YBAHost } from '../../../../../redesign/helpers/constants';
import { RegionOperation } from '../configureRegion/constants';
import { toast } from 'react-toastify';
import { assertUnreachableCase } from '../../../../../utils/errorHandlingUtils';

import { GCPRegionMutation, GCPAvailabilityZoneMutation, YBProviderMutation } from '../../types';

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
  sshKeypairManagement: KeyPairManagement;
  sshKeypairName: string;
  sshPort: number;
  sshPrivateKeyContent: File;
  sshUser: string;
  vpcSetupType: VPCSetupType;
  ybFirewallTags: string;

  [ASYNC_ERROR]: string;
}

const ProviderCredentialType = {
  HOST_INSTANCE_SERVICE_ACCOUNT: 'hostInstanceServiceAccount',
  SPECIFIED_SERVICE_ACCOUNT: 'specifiedServiceAccount'
} as const;
type ProviderCredentialType = typeof ProviderCredentialType[keyof typeof ProviderCredentialType];

const KeyPairManagement = {
  YBA_MANAGED: 'ybaManaged',
  CUSTOM_KEY_PAIR: 'customKeyPair'
} as const;
type KeyPairManagement = typeof KeyPairManagement[keyof typeof KeyPairManagement];

const KEY_PAIR_MANAGEMENT_OPTIONS: OptionProps[] = [
  {
    value: KeyPairManagement.YBA_MANAGED,
    label: 'Use YugabyteDB Anywhere to manage key pairs'
  },
  {
    value: KeyPairManagement.CUSTOM_KEY_PAIR,
    label: 'Provide custom key pair information'
  }
];

const YB_VPC_NAME_BASE = 'yb-gcp-network';
const DEFAULT_ID_SUFFIX_SIZE = 14;

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

  // Specified ssh keys
  sshKeypairName: string().when('sshKeypairManagement', {
    is: KeyPairManagement.CUSTOM_KEY_PAIR,
    then: string().required('SSH keypair name is required.')
  }),
  sshPrivateKeyContent: mixed().when('sshKeypairManagement', {
    is: KeyPairManagement.CUSTOM_KEY_PAIR,
    then: mixed().required('SSH private key is required.')
  }),

  ntpServers: array().when('ntpSetupType', {
    is: NTPSetupType.SPECIFIED,
    then: array().min(1, 'NTP Servers cannot be empty.')
  }),
  regions: array().min(1, 'Provider configurations must contain at least one region.')
});

export const GCPProviderCreateForm = ({
  onBack,
  createInfraProvider
}: GCPProviderCreateFormProps) => {
  const [isRegionFormModalOpen, setIsRegionFormModalOpen] = useState<boolean>(false);
  const [isDeleteRegionModalOpen, setIsDeleteRegionModalOpen] = useState<boolean>(false);
  const [regionSelection, setRegionSelection] = useState<CloudVendorRegionField>();
  const [regionOperation, setRegionOperation] = useState<RegionOperation>(RegionOperation.ADD);

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

  if (hostInfoQuery.isLoading || hostInfoQuery.isIdle) {
    return <YBLoading />;
  }
  if (hostInfoQuery.isError) {
    return <YBErrorIndicator customErrorMessage="Error fetching host info." />;
  }

  const onFormSubmit: SubmitHandler<GCPProviderCreateFormFieldValues> = async (formValues) => {
    formMethods.clearErrors(ASYNC_ERROR);

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
            gceProject: formValues.gceProject ?? googleServiceAccount?.project_id ?? '',
            useHostCredentials: false
          }
        : assertUnreachableCase(formValues.providerCredentialType);

    const providerPayload: YBProviderMutation = {
      code: ProviderCode.GCP,
      name: formValues.providerName,
      ...(formValues.sshKeypairManagement === KeyPairManagement.CUSTOM_KEY_PAIR && {
        ...(formValues.sshKeypairName && { keyPairName: formValues.sshKeypairName }),
        ...(formValues.sshPrivateKeyContent && {
          sshPrivateKeyContent: (await readFileAsText(formValues.sshPrivateKeyContent)) ?? ''
        })
      }),

      details: {
        airGapInstall: !formValues.dbNodePublicInternetAccess,
        cloudInfo: {
          [ProviderCode.GCP]: {
            ...vpcConfig,
            ...gcpCredentials,
            ybFirewallTags: formValues.ybFirewallTags
          }
        },
        ntpServers: formValues.ntpServers,
        setUpChrony: formValues.ntpSetupType !== NTPSetupType.NO_NTP,
        sshPort: formValues.sshPort,
        sshUser: formValues.sshUser
      },
      regions: formValues.regions.map<GCPRegionMutation>((regionFormValues) => ({
        code: regionFormValues.code,
        details: {
          cloudInfo: {
            [ProviderCode.GCP]: {
              ybImage: regionFormValues.ybImage
            }
          }
        },
        zones: GCP_REGIONS[regionFormValues.code]?.zones.map<GCPAvailabilityZoneMutation>(
          (zoneSuffix: string) => ({
            code: `${regionFormValues.code}${zoneSuffix}`,
            name: `${regionFormValues.code}${zoneSuffix}`,
            subnet: regionFormValues.sharedSubnet ?? ''
          })
        )
      }))
    };
    await createInfraProvider(providerPayload, {
      mutateOptions: {
        onError: (error) => handleFormServerError(error, ASYNC_ERROR, formMethods.setError)
      }
    });
  };

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
    formMethods.setValue('regions', regions);
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
  return (
    <Box display="flex" justifyContent="center">
      <FormProvider {...formMethods}>
        <FormContainer name="gcpProviderForm" onSubmit={formMethods.handleSubmit(onFormSubmit)}>
          <Typography variant="h3">Create GCP Provider Configuration</Typography>
          <FormField providerNameField={true}>
            <FieldLabel>Provider Name</FieldLabel>
            <YBInputField control={formMethods.control} name="providerName" fullWidth />
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
                  />
                </FormField>
              )}
              <FormField>
                <FieldLabel>GCE Project Name (Optional Override)</FieldLabel>
                <YBInputField control={formMethods.control} name="gceProject" fullWidth />
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
                        `${YB_VPC_NAME_BASE}-${generateLowerCaseAlphanumericId(
                          DEFAULT_ID_SUFFIX_SIZE
                        )}`
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
                  <YBInputField control={formMethods.control} name="destVpcId" fullWidth />
                </FormField>
              )}
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
                providerCode={ProviderCode.GCP}
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
            <FieldGroup heading="SSH Key Pairs">
              <FormField>
                <FieldLabel>SSH User</FieldLabel>
                <YBInputField control={formMethods.control} name="sshUser" fullWidth />
              </FormField>
              <FormField>
                <FieldLabel>SSH Port</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="sshPort"
                  type="number"
                  inputProps={{ min: 0, max: 65535 }}
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
              {keyPairManagement === KeyPairManagement.CUSTOM_KEY_PAIR && (
                <>
                  <FormField>
                    <FieldLabel>SSH Keypair Name</FieldLabel>
                    <YBInputField control={formMethods.control} name="sshKeypairName" fullWidth />
                  </FormField>
                  <FormField>
                    <FieldLabel>SSH Private Key Content</FieldLabel>
                    <YBDropZoneField
                      name="sshPrivateKeyContent"
                      control={formMethods.control}
                      actionButtonText="Upload SSH Key PEM File"
                      multipleFiles={false}
                      showHelpText={false}
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
                  fullWidth
                />
              </FormField>
              <FormField>
                <FieldLabel>DB Nodes have public internet access?</FieldLabel>
                <YBToggleField name="dbNodePublicInternetAccess" control={formMethods.control} />
              </FormField>
              <FormField>
                <FieldLabel>NTP Setup</FieldLabel>
                <NTPConfigField
                  isDisabled={formMethods.formState.isSubmitting}
                  providerCode={ProviderCode.GCP}
                />
              </FormField>
            </FieldGroup>
          </Box>
          <Box marginTop="16px">
            <YBButton
              btnText="Create Provider Configuration"
              btnClass="btn btn-default save-btn"
              btnType="submit"
              loading={formMethods.formState.isSubmitting}
              disabled={formMethods.formState.isSubmitting}
              data-testid="GCPProviderCreateForm-SubmitButton"
            />
            <YBButton
              btnText="Back"
              btnClass="btn btn-default"
              onClick={onBack}
              disabled={formMethods.formState.isSubmitting}
              data-testid="GCPProviderCreateForm-BackButton"
            />
          </Box>
        </FormContainer>
      </FormProvider>
      {isRegionFormModalOpen && (
        <ConfigureRegionModal
          configuredRegions={regions}
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
