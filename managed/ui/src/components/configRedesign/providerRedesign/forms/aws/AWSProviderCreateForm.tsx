/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import React, { useState } from 'react';
import { AxiosError } from 'axios';
import { Box, CircularProgress, FormHelperText, makeStyles, Typography } from '@material-ui/core';
import { FormProvider, SubmitHandler, useForm } from 'react-hook-form';
import { array, mixed, object, string } from 'yup';
import { yupResolver } from '@hookform/resolvers/yup';
import { useSelector } from 'react-redux';

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
  YBImageType,
  ASYNC_ERROR,
  DEFAULT_SSH_PORT,
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
import { NTP_SERVER_REGEX } from '../constants';
import { YBBanner, YBBannerVariant } from '../../../../common/descriptors';
import { YBButton as YBRedesignedButton } from '../../../../../redesign/components';
import { isAxiosError } from '../../../../../utils/errorHandlingUtils';
import { isNonEmptyObject } from '../../../../../utils/ObjectUtils';
import { getInvalidFields } from './utils';

import { AWSRegionMutation, YBProviderMutation, AWSAvailabilityZoneMutation } from '../../types';
import { YBBeanValidationError, YBPError } from '../../../../../redesign/helpers/dtos';

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
  ybImage: string;
  ybImageType: YBImageType;

  [ASYNC_ERROR]: string;
}

export type QuickValidationErrorKeys = {
  [keyString: string]: string[];
};

const useStyles = makeStyles(() => ({
  errorList: {
    '& li': {
      listStyle: 'disc',
      '&:not(:first-child)': {
        marginTop: '12px'
      }
    }
  },
  loadingIcon: {
    color: '#1890FF'
  }
}));

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

const YBImageTypeLabel = {
  [YBImageType.ARM64]: 'Default AArch64 AMI',
  [YBImageType.X86_64]: 'Default x86 AMI',
  [YBImageType.CUSTOM_AMI]: 'Custom AMI'
};
const YB_IMAGE_TYPE_OPTIONS: OptionProps[] = [
  {
    value: YBImageType.X86_64,
    label: YBImageTypeLabel[YBImageType.X86_64]
  },
  { value: YBImageType.ARM64, label: YBImageTypeLabel[YBImageType.ARM64] },
  {
    value: YBImageType.CUSTOM_AMI,
    label: YBImageTypeLabel[YBImageType.CUSTOM_AMI]
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
  ybImage: string().when('ybImageType', {
    is: YBImageType.CUSTOM_AMI,
    then: string().required('Custom AMI type is required.')
  }),
  ntpServers: array().when('ntpSetupType', {
    is: NTPSetupType.SPECIFIED,
    then: array()
      .min(1, 'NTP Servers cannot be empty.')
      .of(
        string().matches(
          NTP_SERVER_REGEX,
          'NTP servers must be provided in IPv4, IPv6, or hostname format.'
        )
      )
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
  const featureFlags = useSelector((state: any) => state.featureFlags);
  const [
    quickValidationErrors,
    setQuickValidationErrors
  ] = useState<QuickValidationErrorKeys | null>(null);
  const classes = useStyles();

  const defaultValues: Partial<AWSProviderCreateFormFieldValues> = {
    dbNodePublicInternetAccess: true,
    enableHostedZone: false,
    ntpServers: [] as string[],
    ntpSetupType: NTPSetupType.CLOUD_VENDOR,
    providerCredentialType: ProviderCredentialType.ACCESS_KEY,
    regions: [] as CloudVendorRegionField[],
    sshKeypairManagement: KeyPairManagement.YBA_MANAGED,
    sshPort: DEFAULT_SSH_PORT,
    vpcSetupType: VPCSetupType.EXISTING,
    ybImageType: YBImageType.X86_64
  } as const;
  const formMethods = useForm<AWSProviderCreateFormFieldValues>({
    defaultValues: defaultValues,
    resolver: yupResolver(VALIDATION_SCHEMA)
  });

  const constructProviderPayload = async (
    formValues: AWSProviderCreateFormFieldValues
  ): Promise<YBProviderMutation> => ({
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
          ...(formValues.providerCredentialType === ProviderCredentialType.ACCESS_KEY && {
            awsAccessKeyID: formValues.accessKeyId,
            awsAccessKeySecret: formValues.secretAccessKey
          }),
          ...(formValues.enableHostedZone && { awsHostedZoneId: formValues.hostedZoneId })
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
            ...(formValues.ybImageType === YBImageType.CUSTOM_AMI
              ? {
                  ybImage: regionFormValues.ybImage ? regionFormValues.ybImage : formValues.ybImage
                }
              : { arch: formValues.ybImageType }),
            securityGroupId: regionFormValues.securityGroupId,
            vnet: regionFormValues.vnet
          }
        }
      },
      zones: regionFormValues.zones?.map<AWSAvailabilityZoneMutation>((azFormValues) => ({
        code: azFormValues.code,
        name: azFormValues.code,
        subnet: azFormValues.subnet
      }))
    }))
  });

  const clearErrors = () => {
    formMethods.clearErrors(ASYNC_ERROR);
    setQuickValidationErrors(null);
  };
  const onFormSubmit = async (
    formValues: AWSProviderCreateFormFieldValues,
    shouldValidate = true
  ) => {
    clearErrors();
    const providerPayload = await constructProviderPayload(formValues);
    await createInfraProvider(providerPayload, {
      shouldValidate: shouldValidate,
      mutateOptions: { onError: handleFormServerError }
    });
  };
  const onFormValidateAndSubmit: SubmitHandler<AWSProviderCreateFormFieldValues> = async (
    formValues
  ) => await onFormSubmit(formValues, !!featureFlags.test.enableAWSProviderValidation);
  const onFormForceSubmit: SubmitHandler<AWSProviderCreateFormFieldValues> = async (formValues) =>
    await onFormSubmit(formValues, false);

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
  const skipValidationAndSubmit = () => {
    onFormForceSubmit(formMethods.getValues());
  };

  const handleFormServerError = (error: Error | AxiosError<YBBeanValidationError | YBPError>) => {
    if (
      featureFlags.test.enableAWSProviderValidation &&
      isAxiosError<YBBeanValidationError | YBPError>(error) &&
      !(
        typeof error.response?.data.error === 'string' ||
        error.response?.data.error instanceof String
      ) &&
      isNonEmptyObject(error.response?.data.error)
    ) {
      // Handle YBBeanValidationError
      setQuickValidationErrors(error.response?.data.error ?? null);
      const invalidFields = error.response?.data.error
        ? getInvalidFields(error.response?.data.error)
        : [];
      invalidFields.forEach((fieldName) =>
        formMethods.setError(fieldName, {
          type: 'server',
          message:
            'Validation Error. See the field validation failure at the bottom of the page for more details.'
        })
      );
    }

    // Set form error to fail form submission.
    formMethods.setError(ASYNC_ERROR, {
      type: 'server',
      message: error.message ?? 'Server reported error.'
    });
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
  const ybImageType = formMethods.watch('ybImageType', defaultValues.ybImageType);
  return (
    <Box display="flex" justifyContent="center">
      <FormProvider {...formMethods}>
        <FormContainer
          name="awsProviderForm"
          onSubmit={formMethods.handleSubmit(onFormValidateAndSubmit)}
        >
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
                <FieldLabel>{`Use AWS Route 53 DNS Server`}</FieldLabel>
                <YBToggleField name="enableHostedZone" control={formMethods.control} />
              </FormField>
              {enableHostedZone && (
                <FormField>
                  <FieldLabel>Hosted Zone ID</FieldLabel>
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
                <FieldLabel>AMI Type</FieldLabel>
                <YBRadioGroupField
                  name="ybImageType"
                  control={formMethods.control}
                  options={YB_IMAGE_TYPE_OPTIONS}
                  orientation={RadioGroupOrientation.HORIZONTAL}
                />
              </FormField>
              {ybImageType === YBImageType.CUSTOM_AMI && (
                <FormField>
                  <FieldLabel infoContent="This AMI will be used for all regions. A per-region override is available in the 'Add region' modal.">
                    Custom Default AMI
                  </FieldLabel>
                  <YBInputField control={formMethods.control} name="ybImage" fullWidth />
                </FormField>
              )}
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
              {formMethods.formState.errors.regions?.message ? (
                <FormHelperText error={true}>
                  {formMethods.formState.errors.regions?.message}
                </FormHelperText>
              ) : null}
            </FieldGroup>
            <FieldGroup
              heading="SSH Key Pairs"
              infoContent="YBA requires SSH access to DB nodes. For public clouds, YBA provisions the VM instances as part of the DB node provisioning. The OS images come with a preprovisioned user."
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
                <FieldLabel infoContent="If yes, YBA will install some software packages on the DB nodes by downloading from the public internet. If not, all installation of software on the nodes will download from only this YBA instance.">
                  DB Nodes have public internet access?
                </FieldLabel>
                <YBToggleField name="dbNodePublicInternetAccess" control={formMethods.control} />
              </FormField>
              <FormField>
                <FieldLabel>NTP Setup</FieldLabel>
                <NTPConfigField
                  isDisabled={formMethods.formState.isSubmitting}
                  providerCode={ProviderCode.AWS}
                />
              </FormField>
            </FieldGroup>
            {!!featureFlags.test.enableAWSProviderValidation && !!quickValidationErrors && (
              <YBBanner variant={YBBannerVariant.DANGER}>
                <Typography variant="body1">Fields failed validation:</Typography>
                <ul className={classes.errorList}>
                  {Object.entries(quickValidationErrors).map(([keyString, errors]) => {
                    return (
                      <li key={keyString}>
                        {keyString.replace(/^(data\.)/, '')}
                        <ul>
                          {errors.map((error, index) => (
                            <li key={index}>{error}</li>
                          ))}
                        </ul>
                      </li>
                    );
                  })}
                </ul>
                <YBRedesignedButton
                  variant="secondary"
                  onClick={skipValidationAndSubmit}
                  data-testid="AWSProviderCreateForm-SkipValidationButton"
                >
                  Ignore and save provider configuration anyway
                </YBRedesignedButton>
              </YBBanner>
            )}
            {(formMethods.formState.isValidating || formMethods.formState.isSubmitting) && (
              <Box display="flex" gridGap="5px" marginLeft="auto">
                <CircularProgress size={16} color="primary" thickness={5} />
                {!!featureFlags.test.enableAWSProviderValidation && (
                  <Typography variant="body2" color="primary">
                    Validating provider configuration fields... usually take 5-30s to complete.
                  </Typography>
                )}
              </Box>
            )}
          </Box>
          <Box marginTop="16px">
            <YBButton
              btnText={
                featureFlags.test.enableAWSProviderValidation
                  ? 'Validate and Save Configuration'
                  : 'Create Provider Configuration'
              }
              btnClass="btn btn-default save-btn"
              btnType="submit"
              disabled={formMethods.formState.isSubmitting}
              data-testid="AWSProviderCreateForm-SubmitButton"
            />
            <YBButton
              btnText="Back"
              btnClass="btn btn-default"
              onClick={onBack}
              disabled={formMethods.formState.isSubmitting}
              data-testid="AWSProviderCreateForm-BackButton"
            />
          </Box>
        </FormContainer>
      </FormProvider>
      {isRegionFormModalOpen && (
        <ConfigureRegionModal
          onClose={hideRegionFormModal}
          onRegionSubmit={onRegionFormSubmit}
          open={isRegionFormModalOpen}
          providerCode={ProviderCode.AWS}
          regionOperation={regionOperation}
          regionSelection={regionSelection}
          configuredRegions={regions}
          vpcSetupType={vpcSetupType}
          ybImageType={ybImageType}
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
