import { useState } from 'react';
import { array, mixed, object, string } from 'yup';
import { Box, CircularProgress, FormHelperText, Typography } from '@material-ui/core';
import { FormProvider, SubmitHandler, useForm } from 'react-hook-form';
import { yupResolver } from '@hookform/resolvers/yup';
import { toast } from 'react-toastify';

import {
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
  VPCSetupType
} from '../../constants';
import { FieldGroup } from '../components/FieldGroup';
import {
  addItem,
  constructAccessKeysCreatePayload,
  deleteItem,
  editItem,
  getIsFormDisabled,
  readFileAsText
} from '../utils';
import { FormContainer } from '../components/FormContainer';
import { ACCEPTABLE_CHARS } from '../../../../config/constants';
import { FormField } from '../components/FormField';
import { FieldLabel } from '../components/FieldLabel';
import { CreateInfraProvider } from '../../InfraProvider';
import { RegionOperation } from '../configureRegion/constants';
import { NTP_SERVER_REGEX } from '../constants';

import { AZURegionMutation, AZUAvailabilityZoneMutation } from '../../types';
import { RbacValidator } from '../../../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../../../redesign/features/rbac/ApiAndUserPermMapping';

interface AZUProviderCreateFormProps {
  createInfraProvider: CreateInfraProvider;
  onBack: () => void;
}

export interface AZUProviderCreateFormFieldValues {
  azuClientId: string;
  azuClientSecret: string;
  azuHostedZoneId: string;
  azuRG: string;
  azuNetworkRG: string;
  azuSubscriptionId: string;
  azuNetworkSubscriptionId: string;
  azuTenantId: string;
  dbNodePublicInternetAccess: boolean;
  ntpServers: string[];
  ntpSetupType: NTPSetupType;
  providerName: string;
  regions: CloudVendorRegionField[];
  sshKeypairManagement: KeyPairManagement;
  sshKeypairName: string;
  sshPort: number;
  sshPrivateKeyContent: File;
  sshUser: string;
}

export const DEFAULT_FORM_VALUES: Partial<AZUProviderCreateFormFieldValues> = {
  dbNodePublicInternetAccess: true,
  ntpServers: [] as string[],
  ntpSetupType: NTPSetupType.CLOUD_VENDOR,
  providerName: '',
  regions: [] as CloudVendorRegionField[],
  sshKeypairManagement: KeyPairManagement.YBA_MANAGED,
  sshPort: DEFAULT_SSH_PORT
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
  sshPrivateKeyContent: mixed().when('sshKeypairManagement', {
    is: KeyPairManagement.SELF_MANAGED,
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

const FORM_NAME = 'AZUProviderCreateForm';

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

  const onFormSubmit: SubmitHandler<AZUProviderCreateFormFieldValues> = async (formValues) => {
    if (formValues.ntpSetupType === NTPSetupType.SPECIFIED && !formValues.ntpServers.length) {
      formMethods.setError('ntpServers', {
        type: 'min',
        message: 'Please specify at least one NTP server.'
      });
      return;
    }

    try {
      const providerPayload = await constructProviderPayload(formValues);
      try {
        await createInfraProvider(providerPayload);
      } catch (_) {
        // Handled with `mutateOptions.onError`
      }
    } catch (error: any) {
      toast.error(error.message ?? error);
    }
  };

  const regions = formMethods.watch('regions', DEFAULT_FORM_VALUES.regions);
  const setRegions = (regions: CloudVendorRegionField[]) =>
    formMethods.setValue('regions', regions, { shouldValidate: true });
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
  const isFormDisabled = getIsFormDisabled(formMethods.formState);
  return (
    <Box display="flex" justifyContent="center">
      <FormProvider {...formMethods}>
        <FormContainer name="azuProviderForm" onSubmit={formMethods.handleSubmit(onFormSubmit)}>
          <Typography variant="h3">Create Azure Provider Configuration</Typography>
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
                <FieldLabel>Client ID</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="azuClientId"
                  disabled={isFormDisabled}
                  fullWidth
                />
              </FormField>
              <FormField>
                <FieldLabel>Client Secret</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="azuClientSecret"
                  disabled={isFormDisabled}
                  fullWidth
                />
              </FormField>
              <FormField>
                <FieldLabel>Resource Group</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="azuRG"
                  disabled={isFormDisabled}
                  fullWidth
                />
              </FormField>
              <FormField>
                <FieldLabel>Network Resource Group</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="azuNetworkRG"
                  disabled={isFormDisabled}
                  fullWidth
                />
              </FormField>
              <FormField>
                <FieldLabel>Subscription ID</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="azuSubscriptionId"
                  disabled={isFormDisabled}
                  fullWidth
                />
              </FormField>
              <FormField>
                <FieldLabel>Network Subscription ID</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="azuNetworkSubscriptionId"
                  disabled={isFormDisabled}
                  fullWidth
                />
              </FormField>
              <FormField>
                <FieldLabel>Tenant ID</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="azuTenantId"
                  disabled={isFormDisabled}
                  fullWidth
                />
              </FormField>
              <FormField>
                <FieldLabel>Private DNS Zone (Optional)</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="azuHostedZoneId"
                  disabled={isFormDisabled}
                  fullWidth
                />
              </FormField>
            </FieldGroup>
            <FieldGroup
              heading="Regions"
              headerAccessories={
                regions.length > 0 ? (
                  <RbacValidator
                    accessRequiredOn={ApiPermissionMap.CREATE_REGION_BY_PROVIDER}
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
                providerCode={ProviderCode.AZU}
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
            <FieldGroup heading="SSH Key Pairs">
              <FormField>
                <FieldLabel>SSH User</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="sshUser"
                  disabled={isFormDisabled}
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
                  disabled={isFormDisabled}
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
                <NTPConfigField isDisabled={isFormDisabled} providerCode={ProviderCode.AZU} />
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
      {/* Modals */}
      {isRegionFormModalOpen && (
        <ConfigureRegionModal
          configuredRegions={regions}
          isEditProvider={false}
          isProviderFormDisabled={isFormDisabled}
          onClose={hideRegionFormModal}
          onRegionSubmit={onRegionFormSubmit}
          open={isRegionFormModalOpen}
          providerCode={ProviderCode.AZU}
          regionOperation={regionOperation}
          regionSelection={regionSelection}
          vpcSetupType={VPCSetupType.EXISTING}
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

const constructProviderPayload = async (formValues: AZUProviderCreateFormFieldValues) => {
  let sshPrivateKeyContent = '';
  try {
    sshPrivateKeyContent = formValues.sshPrivateKeyContent
      ? (await readFileAsText(formValues.sshPrivateKeyContent)) ?? ''
      : '';
  } catch (error) {
    throw new Error(`An error occurred while processing the SSH private key file: ${error}`);
  }

  const allAccessKeysPayload = constructAccessKeysCreatePayload(
    formValues.sshKeypairManagement,
    formValues.sshKeypairName,
    sshPrivateKeyContent
  );
  return {
    code: ProviderCode.AZU,
    name: formValues.providerName,
    ...allAccessKeysPayload,
    details: {
      airGapInstall: !formValues.dbNodePublicInternetAccess,
      cloudInfo: {
        [ProviderCode.AZU]: {
          azuClientId: formValues.azuClientId,
          azuClientSecret: formValues.azuClientSecret,
          ...(formValues.azuHostedZoneId && { azuHostedZoneId: formValues.azuHostedZoneId }),
          azuRG: formValues.azuRG,
          ...(formValues.azuNetworkRG && { azuNetworkRG: formValues.azuNetworkRG }),
          azuSubscriptionId: formValues.azuSubscriptionId,
          ...(formValues.azuNetworkSubscriptionId && {
            azuNetworkSubscriptionId: formValues.azuNetworkSubscriptionId
          }),
          azuTenantId: formValues.azuTenantId
        }
      },
      ntpServers: formValues.ntpServers,
      setUpChrony: formValues.ntpSetupType !== NTPSetupType.NO_NTP,
      ...(formValues.sshPort && { sshPort: formValues.sshPort }),
      ...(formValues.sshUser && { sshUser: formValues.sshUser })
    },
    regions: formValues.regions.map<AZURegionMutation>((regionFormValues) => ({
      code: regionFormValues.code,
      details: {
        cloudInfo: {
          [ProviderCode.AZU]: {
            ...(regionFormValues.securityGroupId && {
              securityGroupId: regionFormValues.securityGroupId
            }),
            ...(regionFormValues.vnet && {
              vnet: regionFormValues.vnet
            }),
            ...(regionFormValues.ybImage && {
              ybImage: regionFormValues.ybImage
            })
          }
        }
      },
      zones: regionFormValues.zones?.map<AZUAvailabilityZoneMutation>((azFormValues) => ({
        code: azFormValues.code,
        name: azFormValues.code,
        subnet: azFormValues.subnet
      }))
    }))
  };
};
