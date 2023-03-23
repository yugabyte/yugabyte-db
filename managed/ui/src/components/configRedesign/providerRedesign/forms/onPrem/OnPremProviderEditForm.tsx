import React, { useState } from 'react';
import { FormProvider, SubmitHandler, useForm } from 'react-hook-form';
import { Box, Typography } from '@material-ui/core';

import { YBInputField, YBToggleField } from '../../../../../redesign/components';
import { YBButton } from '../../../../common/forms/fields';
import { NTPConfigField } from '../../components/NTPConfigField';
import { RegionList } from '../../components/RegionList';
import { NTPSetupType, ProviderCode } from '../../constants';
import { FieldGroup } from '../components/FieldGroup';
import { FormContainer } from '../components/FormContainer';
import { FormField } from '../components/FormField';
import { FieldLabel } from '../components/FieldLabel';
import { getNtpSetupType } from '../../utils';
import { RegionOperation } from '../configureRegion/constants';
import { ConfigureOnPremRegionFormValues } from '../configureRegion/ConfigureOnPremRegionModal';

import { YBProvider } from '../../types';

interface OnPremProviderEditFormProps {
  providerConfig: YBProvider;
}

interface OnPremProviderEditFormFieldValues {
  dbNodePublicInternetAccess: boolean;
  installNodeExporter: boolean;
  ntpServers: string[];
  ntpSetupType: NTPSetupType;
  providerCredentialType: ProviderCredentialType;
  providerName: string;
  regions: ConfigureOnPremRegionFormValues[];
  skipProvisioning: boolean;
  sshKeypairName: string;
  sshPort: number;
  sshPrivateKeyContent: File;
  sshUser: string;

  nodeExporterPort?: number;
  nodeExporterUser?: string;
  ybHomeDir?: string;
}

const ProviderCredentialType = {
  INSTANCE_SERVICE_ACCOUNT: 'instanceServiceAccount',
  SPECIFIED_SERVICE_ACCOUNT: 'specifiedServiceAccount'
} as const;
type ProviderCredentialType = typeof ProviderCredentialType[keyof typeof ProviderCredentialType];

export const OnPremProviderEditForm = ({ providerConfig }: OnPremProviderEditFormProps) => {
  const [, setIsRegionFormModalOpen] = useState<boolean>(false);
  const [, setIsDeleteRegionModalOpen] = useState<boolean>(false);
  const [, setRegionSelection] = useState<ConfigureOnPremRegionFormValues>();
  const [, setRegionOperation] = useState<RegionOperation>(RegionOperation.ADD);

  const defaultValues = {
    providerName: providerConfig.name,
    sshKeypairName: providerConfig.allAccessKeys?.[0]?.keyInfo.keyPairName,
    dbNodePublicInternetAccess: !providerConfig.details.airGapInstall,
    ntpSetupType: getNtpSetupType(providerConfig),
    installNodeExporter: providerConfig.details.installNodeExporter,
    skipProvisioning: providerConfig.details.skipProvisioning,
    nodeExporterUser: providerConfig.details.nodeExporterUser,
    nodeExporterPort: providerConfig.details.nodeExporterPort,
    ...(providerConfig.code === ProviderCode.ON_PREM && {
      regions: providerConfig.regions,
      sshUser: providerConfig.details.sshUser,
      sshPort: providerConfig.details.sshPort,
      ntpServers: providerConfig.details.ntpServers,
      ybHomeDir: providerConfig.details.cloudInfo.onprem.ybHomeDir
    })
  };
  const formMethods = useForm<OnPremProviderEditFormFieldValues>({
    defaultValues: defaultValues
  });

  const onFormSubmit: SubmitHandler<OnPremProviderEditFormFieldValues> = (formValues) => {};

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

  const regions = formMethods.watch('regions');
  const installNodeExporter = formMethods.watch(
    'installNodeExporter',
    defaultValues.installNodeExporter
  );

  return (
    <Box display="flex" justifyContent="center">
      <FormProvider {...formMethods}>
        <FormContainer name="OnPremProviderForm" onSubmit={formMethods.handleSubmit(onFormSubmit)}>
          <Typography variant="h3">OnPrem Provider Configuration</Typography>
          <FormField providerNameField={true}>
            <FieldLabel>Provider Name</FieldLabel>
            <YBInputField
              control={formMethods.control}
              name="providerName"
              disabled={true}
              fullWidth
            />
          </FormField>
          <Box width="100%" display="flex" flexDirection="column" gridGap="32px">
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
                providerCode={ProviderCode.ON_PREM}
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
                <FieldLabel infoContent="If yes, YBA will install some software packages on the DB nodes by downloading from the public internet. If not, all installation of software on the nodes will download from only this YBA instance.">
                  DB Nodes have public internet access?
                </FieldLabel>
                <YBToggleField
                  name="dbNodePublicInternetAccess"
                  control={formMethods.control}
                  disabled={true}
                />
              </FormField>
              <FormField>
                <FieldLabel infoContent="If enabled, node provisioning will not be done when the universe is created. A pre-provision script will be provided to be run manually instead.">
                  Manually Provision Nodes
                </FieldLabel>
                <YBToggleField
                  name="skipProvisioning"
                  control={formMethods.control}
                  disabled={true}
                />
              </FormField>
              <FormField>
                <FieldLabel>YB Nodes Home Directory (Optional)</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="ybHomeDir"
                  fullWidth
                  disabled={true}
                />
              </FormField>
              <FormField>
                <FieldLabel>Install Node Exporter</FieldLabel>
                <YBToggleField
                  name="installNodeExporter"
                  control={formMethods.control}
                  disabled={true}
                />
              </FormField>
              {installNodeExporter && (
                <FormField>
                  <FieldLabel>Node Exporter User</FieldLabel>
                  <YBInputField
                    control={formMethods.control}
                    name="nodeExporterUser"
                    fullWidth
                    disabled={true}
                  />
                </FormField>
              )}
              <FormField>
                <FieldLabel>Node Exporter Port</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="nodeExporterPort"
                  type="number"
                  fullWidth
                  disabled={true}
                />
              </FormField>
              <FormField>
                <FieldLabel>NTP Setup</FieldLabel>
                <NTPConfigField providerCode={ProviderCode.ON_PREM} isDisabled={true} />
              </FormField>
            </FieldGroup>
          </Box>
        </FormContainer>
      </FormProvider>
    </Box>
  );
};
