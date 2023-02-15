/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import React, { useState } from 'react';
import axios, { AxiosError } from 'axios';
import { Box, FormHelperText, Typography } from '@material-ui/core';
import { FormProvider, SubmitHandler, useForm } from 'react-hook-form';
import { array, mixed, object, string } from 'yup';
import { yupResolver } from '@hookform/resolvers/yup';

import {
  OptionProps,
  RadioGroupOrientation,
  YBInputField,
  YBRadioGroupField,
  YBToggleField
} from '../../../../../redesign/components';
import { YBButton } from '../../../../common/forms/fields';
import { FieldGroup } from '../components/FieldGroup';
import {
  ConfigureRegionModal,
  CloudVendorRegionField
} from '../configureRegion/ConfigureRegionModal';
import {
  ASYNC_ERROR,
  NTPSetupType,
  ProviderCode,
  VPCSetupType,
  VPCSetupTypeLabel
} from '../../constants';
import { RegionList } from '../../components/RegionList';
import { DeleteRegionModal } from '../../components/DeleteRegionModal';
import { YBDropZoneField } from '../../components/YBDropZone/YBDropZoneField';
import { NTPConfigField } from '../../components/NTPConfigField';
import { addItem, deleteItem, editItem, readFileAsText } from '../utils';
import { FormContainer } from '../components/FormContainer';
import { ACCEPTABLE_CHARS } from '../../../../config/constants';
import { FormField } from '../components/FormField';
import { FieldLabel } from '../components/FieldLabel';
import { useQuery } from 'react-query';
import { api, hostInfoQueryKey } from '../../../../../redesign/helpers/api';
import { YBErrorIndicator, YBLoading } from '../../../../common/indicators';
import { getYBAHost } from '../../utils';
import { YBAHost } from '../../../../../redesign/helpers/constants';
import { CreateInfraProvider } from '../../InfraProvider';
import { RegionOperation } from '../configureRegion/constants';

import { AWSRegionMutation, YBProviderMutation, AWSAvailabilityZoneMutation } from '../../types';

interface AWSProviderCreateFormProps {
  createInfraProvider: CreateInfraProvider;
  onBack: () => void;
}

export interface AWSProviderCreateFormFieldValues {
  accessKeyId: string;
  dbNodePublicInternetAccess: boolean;
  enableHostedZone: boolean;
  hostedZoneId: string;
  ntpServers: string[];
  ntpSetupType: NTPSetupType;
  providerCredentialType: ProviderCredentialType;
  providerName: string;
  regions: CloudVendorRegionField[];
  secretAccessKey: string;
  sshKeypairManagement: KeyPairManagement;
  sshKeypairName: string;
  sshPort: number;
  sshPrivateKeyContent: File;
  sshUser: string;
  vpcSetupType: VPCSetupType;

  [ASYNC_ERROR]: string;
}

const ProviderCredentialType = {
  HOST_INSTANCE_IAM_ROLE: 'hostInstanceIAMRole',
  ACCESS_KEY: 'accessKey'
} as const;
type ProviderCredentialType = typeof ProviderCredentialType[keyof typeof ProviderCredentialType];

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

const VALIDATION_SCHEMA = object().shape({
  providerName: string()
    .required('Provider Name is required.')
    .matches(
      ACCEPTABLE_CHARS,
      'Provider name cannot contain special characters other than "-", and "_"'
    ),
  // Specified provider credential types
  accessKeyId: string().when('providerCredentialType', {
    is: ProviderCredentialType.ACCESS_KEY,
    then: string().required('Access key id is required.')
  }),
  secretAccessKey: string().when('providerCredentialType', {
    is: ProviderCredentialType.ACCESS_KEY,
    then: string().required('Secret access key id is required.')
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

  hostedZoneId: string().when('enableHostedZone', {
    is: true,
    then: string().required('Route 53 zone id is required.')
  }),
  ntpServers: array().when('ntpSetupType', {
    is: NTPSetupType.SPECIFIED,
    then: array().min(1, 'NTP Servers cannot be empty.')
  }),
  regions: array().min(1, 'Provider configurations must contain at least one region.')
});

export const AWSProviderCreateForm = ({
  onBack,
  createInfraProvider
}: AWSProviderCreateFormProps) => {
  const [isRegionFormModalOpen, setIsRegionFormModalOpen] = useState<boolean>(false);
  const [isDeleteRegionModalOpen, setIsDeleteRegionModalOpen] = useState<boolean>(false);
  const [regionSelection, setRegionSelection] = useState<CloudVendorRegionField>();
  const [regionOperation, setRegionOperation] = useState<RegionOperation>(RegionOperation.ADD);

  const defaultValues: Partial<AWSProviderCreateFormFieldValues> = {
    dbNodePublicInternetAccess: true,
    enableHostedZone: false,
    ntpServers: [] as string[],
    ntpSetupType: NTPSetupType.CLOUD_VENDOR,
    providerCredentialType: ProviderCredentialType.ACCESS_KEY,
    regions: [] as CloudVendorRegionField[],
    sshKeypairManagement: KeyPairManagement.YBA_MANAGED,
    sshPort: 22,
    vpcSetupType: VPCSetupType.EXISTING
  } as const;
  const formMethods = useForm<AWSProviderCreateFormFieldValues>({
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

  const handleAsyncError = (error: Error | AxiosError) => {
    const errorMessage = axios.isAxiosError(error)
      ? error.response?.data?.error?.message ?? error.message
      : error.message;
    formMethods.setError(ASYNC_ERROR, errorMessage);
  };

  const onFormSubmit: SubmitHandler<AWSProviderCreateFormFieldValues> = async (formValues) => {
    const providerPayload: YBProviderMutation = {
      code: ProviderCode.AWS,
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
          [ProviderCode.AWS]: {
            awsAccessKeyID: formValues.accessKeyId,
            awsAccessKeySecret: formValues.secretAccessKey,
            awsHostedZoneId: formValues.hostedZoneId
          }
        },
        ntpServers: formValues.ntpServers,
        setUpChrony: formValues.ntpSetupType !== NTPSetupType.NO_NTP,
        sshPort: formValues.sshPort,
        sshUser: formValues.sshUser
      },
      regions: formValues.regions.map<AWSRegionMutation>((regionFormValues) => ({
        code: regionFormValues.code,
        details: {
          cloudInfo: {
            [ProviderCode.AWS]: {
              securityGroupId: regionFormValues.securityGroupId,
              vnet: regionFormValues.vnet,
              ybImage: regionFormValues.ybImage
            }
          }
        },
        zones: regionFormValues.zones?.map<AWSAvailabilityZoneMutation>((azFormValues) => ({
          code: azFormValues.code,
          name: azFormValues.code,
          subnet: azFormValues.subnet
        }))
      }))
    };
    await createInfraProvider(providerPayload, { onError: handleAsyncError });
  };

  const credentialOptions: OptionProps[] = [
    {
      value: ProviderCredentialType.ACCESS_KEY,
      label: 'Specify Access ID and Secret Key'
    },
    {
      value: ProviderCredentialType.HOST_INSTANCE_IAM_ROLE,
      label: `Use IAM Role from this YBA host's instance`,
      disabled: getYBAHost(hostInfoQuery.data) !== YBAHost.AWS
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
  const enableHostedZone = formMethods.watch('enableHostedZone', defaultValues.enableHostedZone);
  const vpcSetupType = formMethods.watch('vpcSetupType', defaultValues.vpcSetupType);

  return (
    <Box display="flex" justifyContent="center">
      <FormProvider {...formMethods}>
        <FormContainer name="awsProviderForm" onSubmit={formMethods.handleSubmit(onFormSubmit)}>
          <Typography variant="h3">Create AWS Provider Configuration</Typography>
          <FormField providerNameField={true}>
            <FieldLabel>Provider Name</FieldLabel>
            <YBInputField control={formMethods.control} name="providerName" fullWidth />
          </FormField>
          <Box width="100%" display="flex" flexDirection="column" gridGap="32px">
            <FieldGroup
              heading="Cloud Info"
              infoContent="Enter your cloud credentials and specify how Yugabyte should leverage cloud services."
            >
              <FormField>
                <FieldLabel infoContent="For public cloud Providers YBA creates compute instances, and therefore requires sufficient permissions to do so.">
                  Credential Type
                </FieldLabel>
                <YBRadioGroupField
                  name="providerCredentialType"
                  control={formMethods.control}
                  options={credentialOptions}
                  orientation={RadioGroupOrientation.HORIZONTAL}
                />
              </FormField>
              {providerCredentialType === ProviderCredentialType.ACCESS_KEY && (
                <>
                  <FormField>
                    <FieldLabel>Access Key ID</FieldLabel>
                    <YBInputField control={formMethods.control} name="accessKeyId" fullWidth />
                  </FormField>
                  <FormField>
                    <FieldLabel>Secret Access Key</FieldLabel>
                    <YBInputField control={formMethods.control} name="secretAccessKey" fullWidth />
                  </FormField>
                </>
              )}
              <FormField>
                <FieldLabel>{`Use DNS Server from Cloud`}</FieldLabel>
                <YBToggleField name="enableHostedZone" control={formMethods.control} />
              </FormField>
              {enableHostedZone && (
                <FormField>
                  <FieldLabel>Route 53 Zone ID</FieldLabel>
                  <YBInputField control={formMethods.control} name="hostedZoneId" fullWidth />
                </FormField>
              )}
            </FieldGroup>
            <FieldGroup
              heading="Regions"
              infoContent="Which regions would you like to allow DB nodes to be deployed into?"
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
                providerCode={ProviderCode.AWS}
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
            <FieldGroup
              heading="SSH Key Pairs"
              infoContent="YBA requires SSH access to DB nodes. For public clouds YBA provisions the VM instances as part of the DB node provisioning. The OS images come with a preprovisioned user."
            >
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
                <FieldLabel infoContent="If yes, YBA will install some software packages on the DB nodes by downloading from the public internet. If not, all installation of software on the nodes will download from only this YBA instance.">
                  DB Nodes have public internet access?
                </FieldLabel>
                <YBToggleField name="dbNodePublicInternetAccess" control={formMethods.control} />
              </FormField>
              <FormField>
                <FieldLabel>NTP Setup</FieldLabel>
                <NTPConfigField providerCode={ProviderCode.AWS} />
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
              data-testId="AWSProviderCreateForm-SubmitButton"
            />
            <YBButton
              btnText="Back"
              btnClass="btn btn-default"
              onClick={onBack}
              disabled={formMethods.formState.isSubmitting}
              data-testId="AWSProviderCreateForm-BackButton"
            />
          </Box>
        </FormContainer>
      </FormProvider>
      {/* Modals */}
      {isRegionFormModalOpen && (
        <ConfigureRegionModal
          onClose={hideRegionFormModal}
          onRegionSubmit={onRegionFormSubmit}
          open={isRegionFormModalOpen}
          providerCode={ProviderCode.AWS}
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
