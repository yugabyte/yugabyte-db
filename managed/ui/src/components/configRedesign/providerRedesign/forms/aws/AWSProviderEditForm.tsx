/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import React, { useState } from 'react';
import { FormProvider, SubmitHandler, useForm } from 'react-hook-form';
import { Box, Typography } from '@material-ui/core';

import {
  OptionProps,
  RadioGroupOrientation,
  YBInputField,
  YBRadioGroupField,
  YBToggleField
} from '../../../../../redesign/components';
import { YBButton } from '../../../../common/forms/fields';
import { FieldGroup } from '../components/FieldGroup';
import { CloudVendorRegionField } from '../configureRegion/ConfigureRegionModal';
import { NTPSetupType, ProviderCode, VPCSetupType } from '../../constants';
import { RegionList } from '../../components/RegionList';
import { NTPConfigField } from '../../components/NTPConfigField';
import { FormContainer } from '../components/FormContainer';
import { FormField } from '../components/FormField';
import { getNtpSetupType } from '../../utils';
import { FieldLabel } from '../components/FieldLabel';
import { RegionOperation } from '../configureRegion/constants';

import { YBProvider } from '../../types';

interface AWSProviderEditFormProps {
  providerConfig: YBProvider;
}

export interface AWSProviderEditFormFieldValues {
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
}

const ProviderCredentialType = {
  IAM_ROLE: 'IAMRole',
  ACCESS_KEY: 'accessKey'
} as const;
type ProviderCredentialType = typeof ProviderCredentialType[keyof typeof ProviderCredentialType];

const KeyPairManagement = {
  YBA_MANAGED: 'YBAManaged',
  CUSTOM_KEY_PAIR: 'customKeyPair'
} as const;
type KeyPairManagement = typeof KeyPairManagement[keyof typeof KeyPairManagement];

const CREDENTIAL_OPTIONS: OptionProps[] = [
  {
    value: ProviderCredentialType.ACCESS_KEY,
    label: 'Input Access and Credential Keys'
  },
  {
    value: ProviderCredentialType.IAM_ROLE,
    label: 'Use IAM Role on instance'
  }
];

export const AWSProviderEditForm = ({ providerConfig }: AWSProviderEditFormProps) => {
  const [, setIsRegionFormModalOpen] = useState<boolean>(false);
  const [, setIsDeleteRegionModalOpen] = useState<boolean>(false);
  const [, setRegionSelection] = useState<CloudVendorRegionField>();
  const [, setRegionOperation] = useState<RegionOperation>(RegionOperation.ADD);

  const defaultValues = {
    providerName: providerConfig.name,
    sshKeypairName: providerConfig.allAccessKeys?.[0]?.keyInfo.keyPairName,
    dbNodePublicInternetAccess: !providerConfig.details.airGapInstall,
    ntpSetupType: getNtpSetupType(providerConfig),
    regions: providerConfig.regions,
    ...(providerConfig.code === ProviderCode.AWS && {
      sshUser: providerConfig.details.sshUser,
      sshPort: providerConfig.details.sshPort,
      ntpServers: providerConfig.details.ntpServers,
      accessKeyId: providerConfig.details.cloudInfo.aws.awsAccessKeyID,
      enableHostedZone: !!providerConfig.details.cloudInfo.aws.awsHostedZoneId,
      hostedZoneId: providerConfig.details.cloudInfo.aws.awsHostedZoneId,
      providerCredentialType: providerConfig.details.cloudInfo.aws.awsAccessKeySecret
        ? ProviderCredentialType.ACCESS_KEY
        : ProviderCredentialType.IAM_ROLE,
      secretAccessKey: providerConfig.details.cloudInfo.aws.awsAccessKeySecret
    })
  };
  const formMethods = useForm<AWSProviderEditFormFieldValues>({
    defaultValues: defaultValues
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

  const onFormSubmit: SubmitHandler<AWSProviderEditFormFieldValues> = (formValues) => {};

  const regions = formMethods.watch('regions');
  const providerCredentialType = formMethods.watch('providerCredentialType');
  const enableHostedZone = formMethods.watch('enableHostedZone');
  return (
    <Box display="flex" justifyContent="center">
      <FormProvider {...formMethods}>
        <FormContainer name="awsProviderForm" onSubmit={formMethods.handleSubmit(onFormSubmit)}>
          <Typography variant="h3">AWS Provider Configuration</Typography>
          <FormField providerNameField={true}>
            <FieldLabel>Provider Name</FieldLabel>
            <YBInputField
              control={formMethods.control}
              name="providerName"
              required={true}
              disabled={true}
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
                  options={CREDENTIAL_OPTIONS}
                  orientation={RadioGroupOrientation.HORIZONTAL}
                />
              </FormField>
              {providerCredentialType === ProviderCredentialType.ACCESS_KEY && (
                <>
                  <FormField>
                    <FieldLabel>Access Key ID</FieldLabel>
                    <YBInputField
                      control={formMethods.control}
                      name="accessKeyId"
                      disabled={true}
                      fullWidth
                    />
                  </FormField>
                  <FormField>
                    <FieldLabel>Secret Access Key</FieldLabel>
                    <YBInputField
                      control={formMethods.control}
                      name="secretAccessKey"
                      disabled={true}
                      fullWidth
                    />
                  </FormField>
                </>
              )}
              <FormField>
                <FieldLabel>Enable Hosted Zone</FieldLabel>
                <YBToggleField
                  name="enableHostedZone"
                  control={formMethods.control}
                  disabled={true}
                />
              </FormField>
              {enableHostedZone && (
                <FormField>
                  <FieldLabel>Route 53 Zone ID</FieldLabel>
                  <YBInputField
                    control={formMethods.control}
                    name="hostedZoneId"
                    disabled={true}
                    fullWidth
                  />
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
                providerCode={ProviderCode.AWS}
                regions={regions}
                setRegionSelection={setRegionSelection}
                showAddRegionFormModal={showAddRegionFormModal}
                showEditRegionFormModal={showEditRegionFormModal}
                showDeleteRegionModal={showDeleteRegionModal}
                disabled={formMethods.formState.isSubmitting}
              />
            </FieldGroup>
            <FieldGroup heading="SSH Key Pairs">
              <FormField>
                <FieldLabel>SSH User</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="sshUser"
                  disabled={true}
                  fullWidth
                />
              </FormField>
              <FormField>
                <FieldLabel>SSH Port</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="sshPort"
                  type="number"
                  inputProps={{ min: 0, max: 65535 }}
                  disabled={true}
                  fullWidth
                />
              </FormField>
              <FormField>
                <FieldLabel>SSH Keypair Name</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="sshKeypairName"
                  disabled={true}
                  fullWidth
                />
              </FormField>
            </FieldGroup>
            <FieldGroup heading="Advanced">
              <FormField>
                <FieldLabel>DB Nodes have public internet access?</FieldLabel>
                <YBToggleField
                  name="dbNodePublicInternetAccess"
                  control={formMethods.control}
                  disabled={true}
                />
              </FormField>
              <FormField>
                <FieldLabel>NTP Setup</FieldLabel>
                <NTPConfigField isDisabled={true} providerCode={ProviderCode.AWS} />
              </FormField>
            </FieldGroup>
          </Box>
        </FormContainer>
      </FormProvider>
    </Box>
  );
};
