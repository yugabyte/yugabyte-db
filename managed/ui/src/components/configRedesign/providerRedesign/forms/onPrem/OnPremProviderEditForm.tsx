import { useState } from 'react';
import { Box, CircularProgress, FormHelperText, Typography } from '@material-ui/core';
import { FormProvider, SubmitHandler, useForm } from 'react-hook-form';
import { array, mixed, object, string } from 'yup';
import { yupResolver } from '@hookform/resolvers/yup';
import { toast } from 'react-toastify';

import { KeyPairManagement, NTPSetupType, ProviderCode } from '../../constants';
import { NTP_SERVER_REGEX } from '../constants';
import {
  ConfigureOnPremRegionModal,
  ConfigureOnPremRegionFormValues
} from '../configureRegion/ConfigureOnPremRegionModal';
import { ACCEPTABLE_CHARS } from '../../../../config/constants';
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
import { YBInput, YBInputField, YBToggleField } from '../../../../../redesign/components';
import {
  addItem,
  constructAccessKeysEditPayload,
  deleteItem,
  editItem,
  generateLowerCaseAlphanumericId,
  getIsFormDisabled,
  readFileAsText
} from '../utils';
import { EditProvider } from '../ProviderEditView';
import {
  findExistingRegion,
  findExistingZone,
  getDeletedRegions,
  getDeletedZones,
  getLatestAccessKey,
  getNtpSetupType
} from '../../utils';
import { VersionWarningBanner } from '../components/VersionWarningBanner';
import { getOnPremLocationOption } from '../configureRegion/utils';

import {
  OnPremAvailabilityZone,
  OnPremAvailabilityZoneMutation,
  OnPremProvider,
  OnPremRegion,
  OnPremRegionMutation,
  YBProviderMutation
} from '../../types';
import { RbacValidator } from '../../../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../../../redesign/features/rbac/ApiAndUserPermMapping';

interface OnPremProviderEditFormProps {
  editProvider: EditProvider;
  isProviderInUse: boolean;
  providerConfig: OnPremProvider;
}

interface OnPremProviderEditFormFieldValues {
  dbNodePublicInternetAccess: boolean;
  editSSHKeypair: boolean;
  installNodeExporter: boolean;
  ntpServers: string[];
  ntpSetupType: NTPSetupType;
  providerName: string;
  regions: ConfigureOnPremRegionFormValues[];
  skipProvisioning: boolean;
  sshKeypairName: string;
  sshPort: number | null;
  sshPrivateKeyContent: File;
  sshUser: string;
  version: number;

  nodeExporterPort?: number | null;
  nodeExporterUser?: string;
  ybHomeDir?: string;
}

const VALIDATION_SCHEMA = object().shape({
  providerName: string()
    .required('Provider Name is required.')
    .matches(
      ACCEPTABLE_CHARS,
      'Provider name cannot contain special characters other than "-", and "_"'
    ),
  sshUser: string().required('SSH user is required.'),
  sshPrivateKeyContent: mixed().when('editSSHKeypair', {
    is: true,
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

const FORM_NAME = 'OnPremProviderEditForm';

export const OnPremProviderEditForm = ({
  editProvider,
  isProviderInUse,
  providerConfig
}: OnPremProviderEditFormProps) => {
  const [isRegionFormModalOpen, setIsRegionFormModalOpen] = useState<boolean>(false);
  const [isDeleteRegionModalOpen, setIsDeleteRegionModalOpen] = useState<boolean>(false);
  const [regionSelection, setRegionSelection] = useState<ConfigureOnPremRegionFormValues>();
  const [regionOperation, setRegionOperation] = useState<RegionOperation>(RegionOperation.ADD);

  const defaultValues = constructDefaultFormValues(providerConfig);
  const formMethods = useForm<OnPremProviderEditFormFieldValues>({
    defaultValues: defaultValues,
    resolver: yupResolver(VALIDATION_SCHEMA)
  });

  const onFormSubmit: SubmitHandler<OnPremProviderEditFormFieldValues> = async (formValues) => {
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
    formMethods.setValue('regions', regions, { shouldValidate: true });
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
  const currentProviderVersion = formMethods.watch('version', defaultValues.version);
  const editSSHKeypair = formMethods.watch('editSSHKeypair', defaultValues.editSSHKeypair);
  const latestAccessKey = getLatestAccessKey(providerConfig.allAccessKeys);
  const existingRegions = providerConfig.regions.map((region) => region.code);
  const isFormDisabled = getIsFormDisabled(formMethods.formState, isProviderInUse, providerConfig);
  return (
    <Box display="flex" justifyContent="center">
      <FormProvider {...formMethods}>
        <FormContainer name="OnPremProviderForm" onSubmit={formMethods.handleSubmit(onFormSubmit)}>
          {currentProviderVersion < providerConfig.version && (
            <VersionWarningBanner onReset={onFormReset} dataTestIdPrefix={FORM_NAME} />
          )}
          <Typography variant="h3">Manage OnPrem Provider Configuration</Typography>
          <FormField providerNameField={true}>
            <FieldLabel>Provider Name</FieldLabel>
            <YBInputField
              control={formMethods.control}
              name="providerName"
              fullWidth
              disabled={isFormDisabled}
            />
          </FormField>
          <Box width="100%" display="flex" flexDirection="column" gridGap="32px">
            <FieldGroup
              heading="Regions"
              headerAccessories={
                regions.length > 0 ? (
                  <RbacValidator
                    accessRequiredOn={ApiPermissionMap.MODIFY_REGION_BY_PROVIDER}
                    isControl
                  >
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
                providerCode={ProviderCode.ON_PREM}
                regions={regions}
                existingRegions={existingRegions}
                setRegionSelection={setRegionSelection}
                showAddRegionFormModal={showAddRegionFormModal}
                showEditRegionFormModal={showEditRegionFormModal}
                showDeleteRegionModal={showDeleteRegionModal}
                disabled={isFormDisabled}
                isError={!!formMethods.formState.errors.regions}
                isProviderInUse={isProviderInUse}
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
                  disabled={isFormDisabled}
                />
              </FormField>
              <FormField>
                <FieldLabel>SSH Port</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="sshPort"
                  type="number"
                  inputProps={{ min: 1, max: 65535 }}
                  fullWidth
                  disabled={isFormDisabled}
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
                  disabled={isFormDisabled}
                />
              </FormField>
              {editSSHKeypair && (
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
                <FieldLabel
                  infoTitle="Manually Provision Nodes"
                  infoContent="If enabled, node provisioning will not be done when the universe is created. A pre-provision script will be provided to be run manually instead."
                >
                  Manually Provision Nodes
                </FieldLabel>
                <YBToggleField
                  name="skipProvisioning"
                  control={formMethods.control}
                  disabled={isFormDisabled}
                />
              </FormField>
              <FormField>
                <FieldLabel>YB Nodes Home Directory (Optional)</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="ybHomeDir"
                  fullWidth
                  disabled={isFormDisabled}
                />
              </FormField>
              <FormField>
                <FieldLabel>Install Node Exporter</FieldLabel>
                <YBToggleField
                  name="installNodeExporter"
                  control={formMethods.control}
                  disabled={isFormDisabled}
                />
              </FormField>
              {installNodeExporter && (
                <FormField>
                  <FieldLabel>Node Exporter User</FieldLabel>
                  <YBInputField
                    control={formMethods.control}
                    name="nodeExporterUser"
                    fullWidth
                    disabled={isFormDisabled}
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
                  disabled={isFormDisabled}
                />
              </FormField>
              <FormField>
                <FieldLabel>NTP Setup</FieldLabel>
                <NTPConfigField isDisabled={isFormDisabled} providerCode={ProviderCode.ON_PREM} />
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
              onClick={onFormReset}
              disabled={isFormDisabled}
              data-testid={`${FORM_NAME}-ClearButton`}
            />
          </Box>
        </FormContainer>
      </FormProvider>
      {isRegionFormModalOpen && (
        <ConfigureOnPremRegionModal
          configuredRegions={regions}
          isProviderFormDisabled={isFormDisabled}
          onClose={hideRegionFormModal}
          onRegionSubmit={onRegionFormSubmit}
          open={isRegionFormModalOpen}
          regionSelection={regionSelection}
          regionOperation={regionOperation}
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

const constructDefaultFormValues = (
  providerConfig: OnPremProvider
): Partial<OnPremProviderEditFormFieldValues> => ({
  dbNodePublicInternetAccess: !providerConfig.details.airGapInstall,
  editSSHKeypair: false,
  installNodeExporter: !!providerConfig.details.installNodeExporter,
  nodeExporterPort: providerConfig.details.nodeExporterPort ?? null,
  nodeExporterUser: providerConfig.details.nodeExporterUser ?? '',
  ntpServers: providerConfig.details.ntpServers,
  ntpSetupType: getNtpSetupType(providerConfig),
  providerName: providerConfig.name,
  regions: providerConfig.regions.map((region) => ({
    fieldId: generateLowerCaseAlphanumericId(),
    code: region.code,
    name: region.name || region.code,
    location: getOnPremLocationOption(region.latitude, region.longitude),
    latitude: region.latitude,
    longitude: region.longitude,
    zones: region.zones.map((zone) => ({
      code: zone.code
    }))
  })),
  skipProvisioning: providerConfig.details.skipProvisioning,
  sshPort: providerConfig.details.sshPort ?? null,
  sshUser: providerConfig.details.sshUser ?? '',
  version: providerConfig.version,
  ybHomeDir: providerConfig.details.cloudInfo.onprem.ybHomeDir ?? ''
});

const constructProviderPayload = async (
  formValues: OnPremProviderEditFormFieldValues,
  providerConfig: OnPremProvider
): Promise<YBProviderMutation> => {
  let sshPrivateKeyContent = '';
  try {
    sshPrivateKeyContent = formValues.sshPrivateKeyContent
      ? (await readFileAsText(formValues.sshPrivateKeyContent)) ?? ''
      : '';
  } catch (error) {
    throw new Error(`An error occurred while processing the SSH private key file: ${error}`);
  }

  const allAccessKeysPayload = constructAccessKeysEditPayload(
    formValues.editSSHKeypair,
    KeyPairManagement.SELF_MANAGED,
    { sshKeypairName: formValues.sshKeypairName, sshPrivateKeyContent: sshPrivateKeyContent },
    providerConfig.allAccessKeys
  );

  return {
    code: ProviderCode.ON_PREM,
    name: formValues.providerName,
    ...allAccessKeysPayload,
    details: {
      airGapInstall: !formValues.dbNodePublicInternetAccess,
      cloudInfo: {
        [ProviderCode.ON_PREM]: {
          ...(formValues.ybHomeDir && { ybHomeDir: formValues.ybHomeDir })
        }
      },
      installNodeExporter: formValues.installNodeExporter,
      ...(formValues.nodeExporterPort && { nodeExporterPort: formValues.nodeExporterPort }),
      ...(formValues.nodeExporterUser && { nodeExporterUser: formValues.nodeExporterUser }),
      ntpServers: formValues.ntpServers,
      passwordlessSudoAccess: providerConfig.details.passwordlessSudoAccess,
      provisionInstanceScript: providerConfig.details.provisionInstanceScript,
      setUpChrony: formValues.ntpSetupType !== NTPSetupType.NO_NTP,
      skipProvisioning: formValues.skipProvisioning,
      ...(formValues.sshPort && { sshPort: formValues.sshPort }),
      ...(formValues.sshUser && { sshUser: formValues.sshUser })
    },
    regions: [
      ...formValues.regions.map<OnPremRegionMutation>((regionFormValues) => {
        const existingRegion = findExistingRegion<OnPremProvider, OnPremRegion>(
          providerConfig,
          regionFormValues.code
        );
        return {
          ...(existingRegion && {
            active: existingRegion.active,
            uuid: existingRegion.uuid,
            details: existingRegion.details
          }),
          latitude: regionFormValues.latitude,
          longitude: regionFormValues.longitude,
          code: regionFormValues.code,
          name: regionFormValues.name,
          zones: [
            ...regionFormValues.zones.map((azFormValues) => {
              const existingZone = findExistingZone<OnPremRegion, OnPremAvailabilityZone>(
                existingRegion,
                azFormValues.code
              );
              return {
                ...(existingZone
                  ? {
                    active: existingZone.active,
                    uuid: existingZone.uuid
                  }
                  : { active: true }),
                code: azFormValues.code,
                name: azFormValues.code
              };
            }),
            ...getDeletedZones(existingRegion?.zones, regionFormValues.zones)
          ] as OnPremAvailabilityZoneMutation[]
        };
      }),
      ...getDeletedRegions(providerConfig.regions, formValues.regions)
    ] as OnPremRegionMutation[],
    version: formValues.version
  };
};
