import React, { useState } from 'react';
import { array, mixed, object, string } from 'yup';
import { Box, FormHelperText, Typography } from '@material-ui/core';
import { FormProvider, SubmitHandler, useForm } from 'react-hook-form';
import { yupResolver } from '@hookform/resolvers/yup';

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
import { addItem, deleteItem, editItem, handleFormServerError, readFileAsText } from '../utils';
import { FormContainer } from '../components/FormContainer';
import { ACCEPTABLE_CHARS } from '../../../../config/constants';
import { FormField } from '../components/FormField';
import { FieldLabel } from '../components/FieldLabel';
import { CreateInfraProvider } from '../../InfraProvider';
import { RegionOperation } from '../configureRegion/constants';
import { NTP_SERVER_REGEX } from '../constants';

import { AZURegionMutation, YBProviderMutation, AZUAvailabilityZoneMutation } from '../../types';

interface AZUProviderCreateFormProps {
  createInfraProvider: CreateInfraProvider;
  onBack: () => void;
}

export interface AZUProviderCreateFormFieldValues {
  dbNodePublicInternetAccess: boolean;
  azuClientId: string;
  azuClientSecret: string;
  azuHostedZoneId: string;
  azuRG: string;
  azuSubscriptionId: string;
  azuTenantId: string;
  ntpServers: string[];
  ntpSetupType: NTPSetupType;
  providerName: string;
  regions: CloudVendorRegionField[];
  sshKeypairManagement: KeyPairManagement;
  sshKeypairName: string;
  sshPort: number;
  sshPrivateKeyContent: File;
  sshUser: string;
  vpcSetupType: VPCSetupType;

  [ASYNC_ERROR]: string;
}

const KeyPairManagement = {
  YBA_MANAGED: 'YBAManaged',
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

const VPC_SETUP_OPTIONS: OptionProps[] = [
  {
    value: VPCSetupType.EXISTING,
    label: VPCSetupTypeLabel[VPCSetupType.EXISTING]
  },
  {
    value: VPCSetupType.NEW,
    label: VPCSetupTypeLabel[VPCSetupType.NEW],
    disabled: true
  }
];

export const DEFAULT_FORM_VALUES: Partial<AZUProviderCreateFormFieldValues> = {
  dbNodePublicInternetAccess: true,
  ntpServers: [] as string[],
  ntpSetupType: NTPSetupType.CLOUD_VENDOR,
  providerName: '',
  regions: [] as CloudVendorRegionField[],
  sshKeypairManagement: KeyPairManagement.YBA_MANAGED,
  sshPort: DEFAULT_SSH_PORT,
  vpcSetupType: VPCSetupType.EXISTING
} as const;

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
  // Specified ssh keys
  sshKeypairName: string().when('sshKeypairManagement', {
    is: KeyPairManagement.CUSTOM_KEY_PAIR,
    then: string().required('SSH keypair name is required.')
  }),
  sshPrivateKeyContent: mixed().when('sshKeypairManagement', {
    is: KeyPairManagement.CUSTOM_KEY_PAIR,
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

export const AZUProviderCreateForm = ({
  onBack,
  createInfraProvider
}: AZUProviderCreateFormProps) => {
  const [isRegionFormModalOpen, setIsRegionFormModalOpen] = useState<boolean>(false);
  const [isDeleteRegionModalOpen, setIsDeleteRegionModalOpen] = useState<boolean>(false);
  const [regionSelection, setRegionSelection] = useState<CloudVendorRegionField>();
  const [regionOperation, setRegionOperation] = useState<RegionOperation>(RegionOperation.ADD);
  const formMethods = useForm<AZUProviderCreateFormFieldValues>({
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

  const onFormSubmit: SubmitHandler<AZUProviderCreateFormFieldValues> = async (formValues) => {
    formMethods.clearErrors(ASYNC_ERROR);

    if (formValues.ntpSetupType === NTPSetupType.SPECIFIED && !formValues.ntpServers.length) {
      formMethods.setError('ntpServers', {
        type: 'min',
        message: 'Please specify at least one NTP server.'
      });
      return;
    }

    const providerPayload: YBProviderMutation = {
      code: ProviderCode.AZU,
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
          [ProviderCode.AZU]: {
            azuClientId: formValues.azuClientId,
            azuClientSecret: formValues.azuClientSecret,
            azuHostedZoneId: formValues.azuHostedZoneId,
            azuRG: formValues.azuRG,
            azuSubscriptionId: formValues.azuSubscriptionId,
            azuTenantId: formValues.azuTenantId
          }
        },
        ntpServers: formValues.ntpServers,
        setUpChrony: formValues.ntpSetupType !== NTPSetupType.NO_NTP,
        sshPort: formValues.sshPort,
        sshUser: formValues.sshUser
      },
      regions: formValues.regions.map<AZURegionMutation>((regionFormValues) => ({
        code: regionFormValues.code,
        details: {
          cloudInfo: {
            [ProviderCode.AZU]: {
              securityGroupId: regionFormValues.securityGroupId,
              vnet: regionFormValues.vnet,
              ybImage: regionFormValues.ybImage
            }
          }
        },
        zones: regionFormValues.zones?.map<AZUAvailabilityZoneMutation>((azFormValues) => ({
          code: azFormValues.code,
          subnet: azFormValues.subnet
        }))
      }))
    };
    await createInfraProvider(providerPayload, {
      mutateOptions: {
        onError: (error) => handleFormServerError(error, ASYNC_ERROR, formMethods.setError)
      }
    });
  };

  const regions = formMethods.watch('regions', DEFAULT_FORM_VALUES.regions);
  const setRegions = (regions: CloudVendorRegionField[]) =>
    formMethods.setValue('regions', regions);
  const onRegionFormSubmit = (currentRegion: CloudVendorRegionField) => {
    regionOperation === RegionOperation.ADD
      ? addItem(currentRegion, regions, setRegions)
      : editItem(currentRegion, regions, setRegions);
  };
  const onDeleteRegionSubmit = (currentRegion: CloudVendorRegionField) =>
    deleteItem(currentRegion, regions, setRegions);

  const keyPairManagement = formMethods.watch(
    'sshKeypairManagement',
    DEFAULT_FORM_VALUES.sshKeypairManagement
  );
  const vpcSetupType = formMethods.watch('vpcSetupType', DEFAULT_FORM_VALUES.vpcSetupType);

  return (
    <Box display="flex" justifyContent="center">
      <FormProvider {...formMethods}>
        <FormContainer name="azuProviderForm" onSubmit={formMethods.handleSubmit(onFormSubmit)}>
          <Typography variant="h3">Create Azure Provider Configuration</Typography>
          <FormField providerNameField={true}>
            <FieldLabel>Provider Name</FieldLabel>
            <YBInputField control={formMethods.control} name="providerName" fullWidth />
          </FormField>
          <Box width="100%" display="flex" flexDirection="column" gridGap="32px">
            <FieldGroup heading="Cloud Info">
              <FormField>
                <FieldLabel>Client ID</FieldLabel>
                <YBInputField control={formMethods.control} name="azuClientId" fullWidth />
              </FormField>
              <FormField>
                <FieldLabel>Client Secret</FieldLabel>
                <YBInputField control={formMethods.control} name="azuClientSecret" fullWidth />
              </FormField>
              <FormField>
                <FieldLabel>Resource Group</FieldLabel>
                <YBInputField control={formMethods.control} name="azuRG" fullWidth />
              </FormField>
              <FormField>
                <FieldLabel>Subscription ID</FieldLabel>
                <YBInputField control={formMethods.control} name="azuSubscriptionId" fullWidth />
              </FormField>
              <FormField>
                <FieldLabel>Tenant ID</FieldLabel>
                <YBInputField control={formMethods.control} name="azuTenantId" fullWidth />
              </FormField>
              <FormField>
                <FieldLabel>Private DNS Zone (Optional)</FieldLabel>
                <YBInputField control={formMethods.control} name="azuHostedZoneId" fullWidth />
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
              <FormField>
                <FieldLabel>VPC Setup</FieldLabel>
                <YBRadioGroupField
                  name="vpcSetupType"
                  control={formMethods.control}
                  options={VPC_SETUP_OPTIONS}
                  orientation={RadioGroupOrientation.HORIZONTAL}
                />
              </FormField>
              <RegionList
                providerCode={ProviderCode.AZU}
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
                <FieldLabel>DB Nodes have public internet access?</FieldLabel>
                <YBToggleField name="dbNodePublicInternetAccess" control={formMethods.control} />
              </FormField>
              <FormField>
                <FieldLabel>NTP Setup</FieldLabel>
                <NTPConfigField
                  isDisabled={formMethods.formState.isSubmitting}
                  providerCode={ProviderCode.AZU}
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
              data-testid="AZUProviderCreateForm-SubmitButton"
            />
            <YBButton
              btnText="Back"
              btnClass="btn btn-default"
              onClick={onBack}
              disabled={formMethods.formState.isSubmitting}
              data-testid="AZUProviderCreateForm-BackButton"
            />
          </Box>
        </FormContainer>
      </FormProvider>
      {/* Modals */}
      {isRegionFormModalOpen && (
        <ConfigureRegionModal
          configuredRegions={regions}
          onClose={hideRegionFormModal}
          onRegionSubmit={onRegionFormSubmit}
          open={isRegionFormModalOpen}
          providerCode={ProviderCode.AZU}
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
