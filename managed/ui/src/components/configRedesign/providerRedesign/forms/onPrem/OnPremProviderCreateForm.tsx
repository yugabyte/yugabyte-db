import React, { useState } from 'react';
import { Box, FormHelperText, Typography } from '@material-ui/core';
import { FormProvider, SubmitHandler, useForm } from 'react-hook-form';
import { array, mixed, object, string } from 'yup';
import { yupResolver } from '@hookform/resolvers/yup';

import {
  ASYNC_ERROR,
  DEFAULT_NODE_EXPORTER_PORT,
  DEFAULT_NODE_EXPORTER_USER,
  DEFAULT_SSH_PORT,
  NTPSetupType,
  ProviderCode
} from '../../constants';
import { NTP_SERVER_REGEX } from '../constants';
import {
  ConfigureOnPremRegionModal,
  ConfigureOnPremRegionFormValues
} from '../configureRegion/ConfigureOnPremRegionModal';
import { ACCEPTABLE_CHARS } from '../../../../config/constants';
import { CreateInfraProvider } from '../../InfraProvider';
import { DeleteRegionModal } from '../../components/DeleteRegionModal';
import { FieldGroup } from '../components/FieldGroup';
import { FieldLabel } from '../components/FieldLabel';
import { FormContainer } from '../components/FormContainer';
import { FormField } from '../components/FormField';
import { NTPConfigField } from '../../components/NTPConfigField';
import { RegionList } from '../../components/RegionList';
import { RegionOperation } from '../configureRegion/constants';
import { YBButton } from '../../../../common/forms/fields';
import { YBDropZoneField } from '../../components/YBDropZone/YBDropZoneField';
import { YBInputField, YBToggleField } from '../../../../../redesign/components';
import { addItem, deleteItem, editItem, handleFormServerError, readFileAsText } from '../utils';

import { OnPremRegionMutation, YBProviderMutation } from '../../types';

interface OnPremProviderCreateFormProps {
  createInfraProvider: CreateInfraProvider;
  onBack: () => void;
}

interface OnPremProviderCreateFormFieldValues {
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

  [ASYNC_ERROR]: string;
}

const ProviderCredentialType = {
  INSTANCE_SERVICE_ACCOUNT: 'instanceServiceAccount',
  SPECIFIED_SERVICE_ACCOUNT: 'specifiedServiceAccount'
} as const;
type ProviderCredentialType = typeof ProviderCredentialType[keyof typeof ProviderCredentialType];

const KeyPairManagement = {
  YBA_MANAGED: 'ybaManaged',
  CUSTOM_KEY_PAIR: 'customKeyPair'
} as const;
type KeyPairManagement = typeof KeyPairManagement[keyof typeof KeyPairManagement];

const VALIDATION_SCHEMA = object().shape({
  providerName: string()
    .required('Provider Name is required.')
    .matches(
      ACCEPTABLE_CHARS,
      'Provider name cannot contain special characters other than "-", and "_"'
    ),
  sshUser: string().when('sshKeypairManagement', {
    is: KeyPairManagement.CUSTOM_KEY_PAIR,
    then: string().required('SSH user is required.')
  }),
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

export const OnPremProviderCreateForm = ({
  onBack,
  createInfraProvider
}: OnPremProviderCreateFormProps) => {
  const [isRegionFormModalOpen, setIsRegionFormModalOpen] = useState<boolean>(false);
  const [isDeleteRegionModalOpen, setIsDeleteRegionModalOpen] = useState<boolean>(false);
  const [regionSelection, setRegionSelection] = useState<ConfigureOnPremRegionFormValues>();
  const [regionOperation, setRegionOperation] = useState<RegionOperation>(RegionOperation.ADD);

  const defaultValues = {
    dbNodePublicInternetAccess: true,
    installNodeExporter: true,
    nodeExporterPort: DEFAULT_NODE_EXPORTER_PORT,
    nodeExporterUser: DEFAULT_NODE_EXPORTER_USER,
    ntpServers: [] as string[],
    ntpSetupType: NTPSetupType.SPECIFIED,
    providerName: '',
    regions: [] as ConfigureOnPremRegionFormValues[],
    skipProvisioning: false,
    sshKeypairManagement: KeyPairManagement.CUSTOM_KEY_PAIR,
    sshPort: DEFAULT_SSH_PORT,
    ybHomeDir: ''
  };
  const formMethods = useForm<OnPremProviderCreateFormFieldValues>({
    defaultValues: defaultValues,
    resolver: yupResolver(VALIDATION_SCHEMA)
  });

  const onFormSubmit: SubmitHandler<OnPremProviderCreateFormFieldValues> = async (formValues) => {
    formMethods.clearErrors(ASYNC_ERROR);

    if (formValues.ntpSetupType === NTPSetupType.SPECIFIED && !formValues.ntpServers.length) {
      formMethods.setError('ntpServers', {
        type: 'min',
        message: 'Please specify at least one NTP server.'
      });
      return;
    }

    const providerPayload: YBProviderMutation = {
      code: ProviderCode.ON_PREM,
      name: formValues.providerName,
      allAccessKeys: [
        {
          keyInfo: {
            ...(formValues.sshKeypairName && { keyPairName: formValues.sshKeypairName }),
            ...(formValues.sshPrivateKeyContent && {
              sshPrivateKeyContent: (await readFileAsText(formValues.sshPrivateKeyContent)) ?? ''
            })
          }
        }
      ],
      details: {
        airGapInstall: !formValues.dbNodePublicInternetAccess,
        cloudInfo: {
          [ProviderCode.ON_PREM]: {
            ybHomeDir: formValues.ybHomeDir
          }
        },
        ntpServers: formValues.ntpServers,
        setUpChrony: formValues.ntpSetupType !== NTPSetupType.NO_NTP,
        sshPort: formValues.sshPort,
        sshUser: formValues.sshUser
      },
      regions: formValues.regions.map<OnPremRegionMutation>((regionFormValues) => ({
        code: regionFormValues.code,
        name: regionFormValues.code,
        zones: regionFormValues.zones.map((zone) => ({ code: zone.code, name: zone.code }))
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
  const hideRegionFormModal = () => {
    setIsRegionFormModalOpen(false);
  };
  const showDeleteRegionModal = () => {
    setIsDeleteRegionModalOpen(true);
  };
  const hideDeleteRegionModal = () => {
    setIsDeleteRegionModalOpen(false);
  };

  const regions = formMethods.watch('regions', defaultValues.regions);
  const setRegions = (regions: ConfigureOnPremRegionFormValues[]) =>
    formMethods.setValue('regions', regions);
  const onRegionFormSubmit = (currentRegion: ConfigureOnPremRegionFormValues) => {
    regionOperation === RegionOperation.ADD
      ? addItem(currentRegion, regions, setRegions)
      : editItem(currentRegion, regions, setRegions);
  };
  const onDeleteRegionSubmit = (currentRegion: ConfigureOnPremRegionFormValues) =>
    deleteItem(currentRegion, regions, setRegions);

  const installNodeExporter = formMethods.watch(
    'installNodeExporter',
    defaultValues.installNodeExporter
  );

  const isProviderFormReadOnly = formMethods.formState.isSubmitting;
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
              fullWidth
              disabled={isProviderFormReadOnly}
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
                  disabled={isProviderFormReadOnly}
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
                disabled={isProviderFormReadOnly}
                isError={!!formMethods.formState.errors.regions}
              />
              {formMethods.formState.errors.regions?.message ? (
                <FormHelperText error={true}>
                  {formMethods.formState.errors.regions?.message}
                </FormHelperText>
              ) : null}
            </FieldGroup>
            <FieldGroup heading="SSH Key Pairs">
              <FormField>
                <FieldLabel>SSH User</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="sshUser"
                  fullWidth
                  disabled={isProviderFormReadOnly}
                />
              </FormField>
              <FormField>
                <FieldLabel>SSH Port</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="sshPort"
                  type="number"
                  fullWidth
                  disabled={isProviderFormReadOnly}
                />
              </FormField>
              <FormField>
                <FieldLabel>SSH Keypair Name</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="sshKeypairName"
                  fullWidth
                  disabled={isProviderFormReadOnly}
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
                  disabled={isProviderFormReadOnly}
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
                  disabled={isProviderFormReadOnly}
                />
              </FormField>
              <FormField>
                <FieldLabel infoContent="If enabled, node provisioning will not be done when the universe is created. A pre-provision script will be provided to be run manually instead.">
                  Manually Provision Nodes
                </FieldLabel>
                <YBToggleField
                  name="skipProvisioning"
                  control={formMethods.control}
                  disabled={isProviderFormReadOnly}
                />
              </FormField>
              <FormField>
                <FieldLabel>YB Nodes Home Directory (Optional)</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="ybHomeDir"
                  fullWidth
                  disabled={isProviderFormReadOnly}
                />
              </FormField>
              <FormField>
                <FieldLabel>Install Node Exporter</FieldLabel>
                <YBToggleField
                  name="installNodeExporter"
                  control={formMethods.control}
                  disabled={isProviderFormReadOnly}
                />
              </FormField>
              {installNodeExporter && (
                <FormField>
                  <FieldLabel>Node Exporter User</FieldLabel>
                  <YBInputField
                    control={formMethods.control}
                    name="nodeExporterUser"
                    fullWidth
                    disabled={isProviderFormReadOnly}
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
                  disabled={isProviderFormReadOnly}
                />
              </FormField>
              <FormField>
                <FieldLabel>NTP Setup</FieldLabel>
                <NTPConfigField
                  isDisabled={isProviderFormReadOnly}
                  providerCode={ProviderCode.ON_PREM}
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
              disabled={isProviderFormReadOnly}
            />
            <YBButton
              btnText="Back"
              btnClass="btn btn-default"
              onClick={onBack}
              disabled={formMethods.formState.isSubmitting}
            />
          </Box>
        </FormContainer>
      </FormProvider>
      {isRegionFormModalOpen && (
        <ConfigureOnPremRegionModal
          configuredRegions={regions}
          onClose={hideRegionFormModal}
          onRegionSubmit={onRegionFormSubmit}
          open={isRegionFormModalOpen}
          regionSelection={regionSelection}
        />
      )}
      {isDeleteRegionModalOpen && (
        <DeleteRegionModal
          region={regionSelection}
          onClose={hideDeleteRegionModal}
          open={isDeleteRegionModalOpen}
          deleteRegion={onDeleteRegionSubmit}
        />
      )}
    </Box>
  );
};
