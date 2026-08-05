import { useState } from 'react';
import { array, mixed, object, string } from 'yup';
import { Box, FormHelperText, Typography } from '@material-ui/core';
import { FormProvider, SubmitHandler, useForm } from 'react-hook-form';
import { yupResolver } from '@hookform/resolvers/yup';
import { toast } from 'react-toastify';
import { useQuery } from 'react-query';

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
import {
  DEFAULT_SSH_PORT,
  KeyPairManagement,
  KEY_PAIR_MANAGEMENT_OPTIONS,
  NTPSetupType,
  ProviderCode,
  ProviderOperation,
  VPCSetupType,
  SshPrivateKeyInputType
} from '../../constants';
import { FieldGroup } from '../components/FieldGroup';
import {
  addItem,
  constructAccessKeysCreatePayload,
  deleteItem,
  editItem,
  getIsFormDisabled,
  readFileAsText,
  handleFormSubmitServerError,
  useIsProviderValidationEnabled
} from '../utils';
import { FormContainer } from '../components/FormContainer';
import { ACCEPTABLE_CHARS } from '../../../../config/constants';
import { FormField } from '../components/FormField';
import { FieldLabel } from '../components/FieldLabel';
import { SubmitInProgress } from '../components/SubmitInProgress';
import { CreateInfraProvider } from '../../InfraProvider';
import { RegionOperation } from '../configureRegion/constants';
import { NTP_SERVER_REGEX } from '../constants';
import { getRegionOptions } from '../configureRegion/utils';
import { ReactSelectOption, YBReactSelectField } from '../../components/YBReactSelect/YBReactSelectField';

import { QUERY_KEY } from '../../../../../redesign/features/universe/universe-form/utils/api';
import {
  OCIAvailabilityZoneMutation,
  OCIRegionMutation,
  YBProviderMutation
} from '../../types';
import { RbacValidator } from '../../../../../redesign/features/rbac/common/RbacApiPermValidator';
import {
  ConfigureSSHDetailsMsg,
  IsOsPatchingEnabled,
  constructImageBundlePayload
} from '../../components/linuxVersionCatalog/LinuxVersionUtils';
import { ApiPermissionMap } from '../../../../../redesign/features/rbac/ApiAndUserPermMapping';
import { LinuxVersionCatalog } from '../../components/linuxVersionCatalog/LinuxVersionCatalog';
import { CloudType } from '../../../../../redesign/helpers/dtos';
import { ImageBundle } from '../../../../../redesign/features/universe/universe-form/utils/dto';
import { fetchGlobalRunTimeConfigs } from '../../../../../api/admin';
import { YBErrorIndicator, YBLoading } from '../../../../common/indicators';
import { api, regionMetadataQueryKey } from '../../../../../redesign/helpers/api';
import { OCI_FORM_MAPPERS, OCID_REGEX, OCI_FINGERPRINT_REGEX } from './constants';
import { OciApiPrivateKeyField } from '../../components/OciApiPrivateKeyField';
import { SshPrivateKeyFormField } from '../../components/SshPrivateKeyField';

interface OCIProviderCreateFormProps {
  createInfraProvider: CreateInfraProvider;
  onBack: () => void;
}

export interface OCIProviderCreateFormFieldValues {
  ociTenancyId: string;
  ociUserId: string;
  ociFingerprint: string;
  ociPrivateKeyContent: File;
  ociCompartmentId: string;
  ociRegionData: ReactSelectOption | null;
  ociHostedZoneId: string;
  dbNodePublicInternetAccess: boolean;
  ntpServers: string[];
  ntpSetupType: NTPSetupType;
  providerName: string;
  regions: CloudVendorRegionField[];
  sshKeypairManagement: KeyPairManagement;
  sshKeypairName: string;
  sshPort: number;
  sshPrivateKeyInputType: SshPrivateKeyInputType;
  sshPrivateKeyContentText: string;
  sshPrivateKeyContent: File;
  sshUser: string;
  imageBundles: ImageBundle[];
}

export const DEFAULT_FORM_VALUES: Partial<OCIProviderCreateFormFieldValues> = {
  dbNodePublicInternetAccess: true,
  ntpServers: [] as string[],
  ntpSetupType: NTPSetupType.CLOUD_VENDOR,
  providerName: '',
  regions: [] as CloudVendorRegionField[],
  sshPrivateKeyInputType: SshPrivateKeyInputType.UPLOAD_KEY,
  sshKeypairManagement: KeyPairManagement.YBA_MANAGED,
  sshPort: DEFAULT_SSH_PORT,
  sshUser: 'opc'
} as const;

const VALIDATION_SCHEMA = object().shape({
  providerName: string()
    .required('Provider Name is required.')
    .matches(
      ACCEPTABLE_CHARS,
      'Provider name cannot contain special characters other than "-", and "_"'
    ),
  ociTenancyId: string()
    .required('Tenancy OCID is required.')
    .matches(OCID_REGEX, 'Tenancy OCID must be in OCID format: ocid1.<type>.<realm>.<unique_id>'),
  ociUserId: string()
    .required('User OCID is required.')
    .matches(OCID_REGEX, 'User OCID must be in OCID format: ocid1.<type>.<realm>.<unique_id>'),
  ociFingerprint: string()
    .required('Fingerprint is required.')
    .matches(OCI_FINGERPRINT_REGEX, 'Fingerprint must be 16 colon-separated hexadecimal pairs'),
  ociPrivateKeyContent: mixed().required('API private key is required.'),
  ociCompartmentId: string()
    .required('Compartment OCID is required.')
    .matches(
      OCID_REGEX,
      'Compartment OCID must be in OCID format: ocid1.<type>.<realm>.<unique_id>'
    ),
  ociRegionData: object().required('Default region is required.'),
  ociHostedZoneId: string().matches(OCID_REGEX, {
    message: 'DNS Zone OCID must be in OCID format: ocid1.<type>.<realm>.<unique_id>',
    excludeEmptyString: true
  }),
  sshPrivateKeyContent: mixed().when(['sshKeypairManagement', 'sshPrivateKeyInputType'], {
    is: (sshKeypairManagement, sshPrivateKeyInputType) =>
      sshKeypairManagement === KeyPairManagement.SELF_MANAGED &&
      sshPrivateKeyInputType === SshPrivateKeyInputType.UPLOAD_KEY,
    then: mixed().required('SSH private key is required.')
  }),
  sshPrivateKeyContentText: string().when(['sshKeypairManagement', 'sshPrivateKeyInputType'], {
    is: (sshKeypairManagement, sshPrivateKeyInputType) =>
      sshKeypairManagement === KeyPairManagement.SELF_MANAGED &&
      sshPrivateKeyInputType === SshPrivateKeyInputType.PASTE_KEY,
    then: string().required('SSH private key is required.')
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

const FORM_NAME = 'OCIProviderCreateForm';

export const OCIProviderCreateForm = ({
  onBack,
  createInfraProvider
}: OCIProviderCreateFormProps) => {
  const [isRegionFormModalOpen, setIsRegionFormModalOpen] = useState<boolean>(false);
  const [isDeleteRegionModalOpen, setIsDeleteRegionModalOpen] = useState<boolean>(false);
  const [regionSelection, setRegionSelection] = useState<CloudVendorRegionField>();
  const [regionOperation, setRegionOperation] = useState<RegionOperation>(RegionOperation.ADD);
  const [isValidationErrorExist, setValidationErrorExist] = useState(false);
  const formMethods = useForm<OCIProviderCreateFormFieldValues>({
    defaultValues: DEFAULT_FORM_VALUES,
    resolver: yupResolver(VALIDATION_SCHEMA)
  });

  useQuery(QUERY_KEY.fetchGlobalRunTimeConfigs, () =>
    fetchGlobalRunTimeConfigs(true).then((res: any) => res.data)
  );
  const { isRuntimeConfigLoading, isValidationEnabled } = useIsProviderValidationEnabled(
    CloudType.oci
  );
  const regionMetadataQuery = useQuery(regionMetadataQueryKey.detail(ProviderCode.OCI), () =>
    api.fetchRegionMetadata(ProviderCode.OCI)
  );

  const isOsPatchingEnabled = IsOsPatchingEnabled();
  const sshConfigureMsg = ConfigureSSHDetailsMsg();

  if (regionMetadataQuery.isLoading || isRuntimeConfigLoading) {
    return <YBLoading />;
  }

  if (regionMetadataQuery.isError) {
    return (
      <YBErrorIndicator customErrorMessage="Error fetching region metadata." />
    );
  }

  const regionOptions = getRegionOptions(regionMetadataQuery.data!);

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

  const onFormSubmit = async (
    formValues: OCIProviderCreateFormFieldValues,
    shouldValidate: boolean,
    ignoreValidationErrors = false
  ) => {
    if (formValues.ntpSetupType === NTPSetupType.SPECIFIED && !formValues.ntpServers.length) {
      formMethods.setError('ntpServers', {
        type: 'min',
        message: 'Please specify at least one NTP server.'
      });
      return;
    }

    try {
      setValidationErrorExist(false);
      const providerPayload = await constructProviderPayload(formValues);
      try {
        await createInfraProvider(providerPayload, {
          shouldValidate: shouldValidate,
          ignoreValidationErrors: ignoreValidationErrors,
          mutateOptions: {
            onError: (err) => {
              handleFormSubmitServerError(
                (err as any)?.response?.data,
                formMethods,
                OCI_FORM_MAPPERS
              );
              setValidationErrorExist(true);
            }
          }
        });
      } catch (_) {
        // Handled with `mutateOptions.onError`
      }
    } catch (error: any) {
      toast.error(error.message ?? error);
    }
  };

  const onFormValidateAndSubmit: SubmitHandler<OCIProviderCreateFormFieldValues> = async (
    formValues
  ) => await onFormSubmit(formValues, isValidationEnabled);
  const onFormForceSubmit: SubmitHandler<OCIProviderCreateFormFieldValues> = async (formValues) =>
    await onFormSubmit(formValues, isValidationEnabled, true);

  const skipValidationAndSubmit = () => {
    onFormForceSubmit(formMethods.getValues());
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
        <FormContainer
          name="ociProviderForm"
          onSubmit={formMethods.handleSubmit(onFormValidateAndSubmit)}
        >
          <Typography variant="h3">Create OCI Provider Configuration</Typography>
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
                <FieldLabel>Tenancy OCID</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="ociTenancyId"
                  disabled={isFormDisabled}
                  fullWidth
                />
              </FormField>
              <FormField>
                <FieldLabel>User OCID</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="ociUserId"
                  disabled={isFormDisabled}
                  fullWidth
                />
              </FormField>
              <FormField>
                <FieldLabel>Fingerprint</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="ociFingerprint"
                  disabled={isFormDisabled}
                  fullWidth
                />
              </FormField>
              <OciApiPrivateKeyField isFormDisabled={isFormDisabled} />
              <FormField>
                <FieldLabel>Compartment OCID</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="ociCompartmentId"
                  disabled={isFormDisabled}
                  fullWidth
                />
              </FormField>
              <FormField>
                <FieldLabel>Default Region</FieldLabel>
                <YBReactSelectField
                  control={formMethods.control}
                  name="ociRegionData"
                  options={regionOptions}
                  isDisabled={isFormDisabled}
                  placeholder="Select region..."
                />
              </FormField>
              <FormField>
                <FieldLabel>DNS Zone OCID (Optional)</FieldLabel>
                <YBInputField
                  control={formMethods.control}
                  name="ociHostedZoneId"
                  disabled={isFormDisabled}
                  fullWidth
                />
              </FormField>
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
                providerCode={ProviderCode.OCI}
                providerOperation={ProviderOperation.CREATE}
                regions={regions}
                setRegionSelection={setRegionSelection}
                showAddRegionFormModal={showAddRegionFormModal}
                showEditRegionFormModal={showEditRegionFormModal}
                showDeleteRegionModal={showDeleteRegionModal}
                isDisabled={isFormDisabled}
                isError={!!formMethods.formState.errors.regions}
                errors={formMethods.formState.errors.regions as any}
              />
              {formMethods.formState.errors.regions?.message && (
                <FormHelperText error={true}>
                  {formMethods.formState.errors.regions?.message}
                </FormHelperText>
              )}
            </FieldGroup>
            <LinuxVersionCatalog
              control={formMethods.control as any}
              providerType={ProviderCode.OCI}
              providerOperation={ProviderOperation.CREATE}
              isDisabled={isFormDisabled}
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
                  <SshPrivateKeyFormField
                    isFormDisabled={isFormDisabled}
                    providerCode={ProviderCode.OCI}
                  />
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
                <NTPConfigField isDisabled={isFormDisabled} providerCode={ProviderCode.OCI} />
              </FormField>
            </FieldGroup>
            {(formMethods.formState.isValidating || formMethods.formState.isSubmitting) && (
              <SubmitInProgress isValidationEnabled={isValidationEnabled} />
            )}
          </Box>
          <Box marginTop="16px">
            <YBButton
              btnText={
                isValidationEnabled
                  ? 'Validate and Save Configuration'
                  : 'Create Provider Configuration'
              }
              btnClass="btn btn-default save-btn"
              btnType="submit"
              disabled={isFormDisabled || formMethods.formState.isValidating}
              data-testid={`${FORM_NAME}-SubmitButton`}
            />
            {isValidationEnabled && isValidationErrorExist && (
              <YBButton
                btnText="Ignore and save provider configuration anyway"
                btnClass="btn btn-default float-right mr-10"
                onClick={skipValidationAndSubmit}
                disabled={isFormDisabled || formMethods.formState.isValidating}
                data-testid={`${FORM_NAME}-IgnoreAndSave`}
              />
            )}
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
          providerCode={ProviderCode.OCI}
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

const constructProviderPayload = async (
  formValues: OCIProviderCreateFormFieldValues
): Promise<YBProviderMutation> => {
  let ociPrivateKeyContent = '';
  try {
    ociPrivateKeyContent = formValues.ociPrivateKeyContent
      ? (await readFileAsText(formValues.ociPrivateKeyContent)) ?? ''
      : '';
  } catch (error) {
    throw new Error(`An error occurred while processing the API private key file: ${error}`);
  }

  let sshPrivateKeyContent = '';
  if (formValues.sshPrivateKeyInputType === SshPrivateKeyInputType.UPLOAD_KEY) {
    try {
      sshPrivateKeyContent = formValues.sshPrivateKeyContent
        ? (await readFileAsText(formValues.sshPrivateKeyContent)) ?? ''
        : '';
    } catch (error) {
      throw new Error(`An error occurred while processing the SSH private key file: ${error}`);
    }
  } else {
    sshPrivateKeyContent = formValues.sshPrivateKeyContentText;
  }

  const imageBundles = constructImageBundlePayload(formValues);

  const allAccessKeysPayload = constructAccessKeysCreatePayload(
    formValues.sshKeypairManagement,
    formValues.sshKeypairName,
    sshPrivateKeyContent
  );
  return {
    code: ProviderCode.OCI,
    name: formValues.providerName,
    ...allAccessKeysPayload,
    details: {
      airGapInstall: !formValues.dbNodePublicInternetAccess,
      cloudInfo: {
        [ProviderCode.OCI]: {
          ociTenancyId: formValues.ociTenancyId,
          ociUserId: formValues.ociUserId,
          ociFingerprint: formValues.ociFingerprint,
          ociPrivateKeyContent,
          ociCompartmentId: formValues.ociCompartmentId,
          ociRegion: formValues.ociRegionData?.value?.code ?? '',
          ...(formValues.ociHostedZoneId && { ociHostedZoneId: formValues.ociHostedZoneId })
        }
      },
      ntpServers: formValues.ntpServers,
      setUpChrony: formValues.ntpSetupType !== NTPSetupType.NO_NTP,
      ...(formValues.sshPort && { sshPort: formValues.sshPort }),
      ...(formValues.sshUser && { sshUser: formValues.sshUser })
    },
    regions: formValues.regions.map<OCIRegionMutation>((regionFormValues) => ({
      code: regionFormValues.code,
      zones: regionFormValues.zones?.map<OCIAvailabilityZoneMutation>((azFormValues) => ({
        code: azFormValues.code,
        name: azFormValues.code,
        subnet: azFormValues.subnet
      }))
    })),
    imageBundles
  };
};
