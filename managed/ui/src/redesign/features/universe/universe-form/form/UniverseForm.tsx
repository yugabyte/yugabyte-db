import { useContext, FC } from 'react';
import _ from 'lodash';
import { useTranslation } from 'react-i18next';
import { useForm, FormProvider } from 'react-hook-form';
import { useQuery } from 'react-query';
import { useSelector } from 'react-redux';
import { toast } from 'react-toastify';
import { Typography, Grid, Box } from '@material-ui/core';
import { YBButton } from '../../../../components';
import {
  AdvancedConfiguration,
  CloudConfiguration,
  GFlags,
  HelmOverrides,
  InstanceConfiguration,
  SecurityConfiguration,
  UserTags,
  UniverseResourceContainer
} from './sections';
import { UniverseFormContext } from '../UniverseFormContainer';
import { api, QUERY_KEY } from '../utils/api';
import { UniverseFormData, ClusterType, ClusterModes } from '../utils/dto';
import { UNIVERSE_NAME_FIELD, TOAST_AUTO_DISMISS_INTERVAL } from '../utils/constants';
import { useFormMainStyles } from '../universeMainStyle';
import { RbacValidator, hasNecessaryPerm } from '../../../rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../rbac/ApiAndUserPermMapping';

// ! How to add new form field ?
// - add field to it's corresponding config type (CloudConfigFormValue/InstanceConfigFormValue/AdvancedConfigFormValue/..) present in UniverseFormData type at dto.ts
// - set default value in corresponding const (DEFAULT_CLOUD_CONFIG/DEFAULT_INSTANCE_CONFIGDEFAULT_ADVANCED_CONFIG/..) presnt in DEFAULT_FORM_DATA at dto.ts
// - populate form value from universe object in getFormData() at utils/helpers
// - Map form value to universe payload in getUserIntent() at utils/helper before submitting
// - Submit logic/flags needed for each cluster operation is written in it's own file(CreateUniverse/CreateRR/EditUniverse/EditRR)
// - Import actual form field ui component in corresponding section(cloud/instance/advanced/..)

// ! Field component rules and requirements
// - should update itself only and don't modify other form fields
// - should have custom field logic and field validation logic, if needed
// - could watch other form field values

interface UniverseFormProps {
  defaultFormData: UniverseFormData;
  onFormSubmit: (data: UniverseFormData) => void;
  onCancel: () => void;
  onClusterTypeChange?: (data: UniverseFormData) => void;
  onDeleteRR?: () => void;
  submitLabel?: string;
  isNewUniverse?: boolean; // This flag is used only in new cluster creation flow - we don't have proper state params to differentiate
  universeUUID?: string;
  isViewMode?: boolean;
}

export const UniverseForm: FC<UniverseFormProps> = ({
  defaultFormData,
  onFormSubmit,
  onCancel,
  onClusterTypeChange,
  onDeleteRR,
  submitLabel,
  universeUUID,
  isNewUniverse = false,
  isViewMode = false
}) => {
  const classes = useFormMainStyles();
  const { t } = useTranslation();

  //context state
  const {
    asyncFormData,
    clusterType,
    mode,
    universeResourceTemplate,
    universeConfigureError
  } = useContext(UniverseFormContext)[0];
  const isPrimary = clusterType === ClusterType.PRIMARY;
  const isEditMode = mode === ClusterModes.EDIT;
  const isEditRR = isEditMode && !isPrimary;

  // Fetch customer scope runtime configs
  const currentCustomer = useSelector((state: any) => state.customer.currentCustomer);
  const customerUUID = currentCustomer?.data?.uuid;
  const { data: runtimeConfigs } = useQuery(
    [QUERY_KEY.fetchCustomerRunTimeConfigs, customerUUID],
    () => api.fetchRunTimeConfigs(true, customerUUID)
  );

  //init form
  const formMethods = useForm<UniverseFormData>({
    mode: 'onChange',
    reValidateMode: 'onChange',
    defaultValues: defaultFormData,
    shouldFocusError: true
  });
  const { getValues, trigger } = formMethods;

  //methods
  const triggerValidation = () => trigger(undefined, { shouldFocus: true }); //Trigger validation and focus on fields with errors , undefined = validate all fields

  const onSubmit = (formData: UniverseFormData) => {
    if (!_.isEmpty(universeConfigureError))
      // Do not allow for form submission incase error exists in universe configure response
      toast.error(universeConfigureError, { autoClose: TOAST_AUTO_DISMISS_INTERVAL });
    else onFormSubmit(formData);
  };
  const switchClusterType = () => onClusterTypeChange && onClusterTypeChange(getValues());

  //switching from primary to RR and vice versa  (Create Primary + RR flow)
  const handleClusterChange = async () => {
    if (isPrimary) {
      // Validate primary form before switching to async
      if (!_.isEmpty(universeConfigureError))
        toast.error(universeConfigureError, { autoClose: TOAST_AUTO_DISMISS_INTERVAL });
      let isValid = await triggerValidation();
      isValid && switchClusterType();
    } else {
      //switching from async to primary
      switchClusterType();
    }
  };

  const renderHeader = () => {
    return (
      <>
        <Typography className={classes.headerFont}>
          {isNewUniverse ? (
            t('universeForm.createUniverse')
          ) : (
            <a href={`/universes/${universeUUID}`} className={classes.headerText}>
              {getValues(UNIVERSE_NAME_FIELD)}
            </a>
          )}
        </Typography>
        {!isNewUniverse && (
          <Typography className={classes.subHeaderFont}>
            <i className="fa fa-chevron-right"></i> &nbsp;
            {isPrimary
              ? isViewMode
                ? t('universeForm.viewPrimary')
                : t('universeForm.editUniverse')
              : isViewMode
              ? t('universeForm.viewReadReplica')
              : t('universeForm.configReadReplica')}
          </Typography>
        )}
        {onClusterTypeChange && (
          <>
            <Box
              flexShrink={1}
              display={'flex'}
              ml={5}
              alignItems="center"
              className={isPrimary ? classes.selectedTab : classes.disabledTab}
            >
              {t('universeForm.primaryTab')}
            </Box>
            <Box
              flexShrink={1}
              display={'flex'}
              ml={5}
              mr={1}
              alignItems="center"
              className={!isPrimary ? classes.selectedTab : classes.disabledTab}
            >
              {t('universeForm.rrTab')}
            </Box>
            {/* show during new universe creation only */}
            {!isViewMode && isNewUniverse && onDeleteRR && !!asyncFormData && (
              <YBButton
                className={classes.clearRRButton}
                variant="secondary"
                size="medium"
                data-testid="UniverseForm-ClearRR"
                onClick={onDeleteRR}
              >
                {t('universeForm.clearReadReplica')}
              </YBButton>
            )}
          </>
        )}
      </>
    );
  };

  const renderFooter = () => {
    return (
      <>
        <Grid container justifyContent="space-between">
          <Grid item lg={6}>
            <Box
              width="100%"
              display="flex"
              flexShrink={1}
              justifyContent="flex-start"
              alignItems="center"
            >
              <UniverseResourceContainer data={universeResourceTemplate} />
            </Box>
          </Grid>
          <Grid item lg={6}>
            <Box
              width="100%"
              display="flex"
              justifyContent="flex-end"
              className={classes.universeFormButtons}
            >
              <YBButton
                variant="secondary"
                size="large"
                onClick={onCancel}
                data-testid="UniverseForm-Cancel"
              >
                {t('common.cancel')}
              </YBButton>
              &nbsp;
              {/* shown only during create primary + RR flow ( fresh universe ) */}
              {onClusterTypeChange && (
                <YBButton
                  variant="secondary"
                  size="large"
                  onClick={handleClusterChange}
                  data-testid="UniverseForm-ClusterChange"
                >
                  {isPrimary
                    ? t('universeForm.actions.configureRR')
                    : t('universeForm.actions.backPrimary')}
                </YBButton>
              )}
              {/* shown only during edit RR flow */}
              {onDeleteRR && isEditRR && (
                <RbacValidator
                  accessRequiredOn={{
                    onResource: universeUUID,
                    ...ApiPermissionMap.DELETE_READ_REPLICA
                  }}
                  isControl
                >
                  <YBButton
                    variant="secondary"
                    size="large"
                    onClick={onDeleteRR}
                    data-testid="UniverseForm-DeleteRR"
                  >
                    {t('universeForm.actions.deleteRR')}
                  </YBButton>
                </RbacValidator>
              )}
              &nbsp;
              <RbacValidator
                customValidateFunction={() => {
                  //create mode
                  if (!isEditMode) {
                    // we are creating primary
                    if (isPrimary) return hasNecessaryPerm(ApiPermissionMap.CREATE_UNIVERSE);
                    // if the universe is already created , then we need update universe perm
                    // or else we need universe create perm
                    return universeUUID === undefined
                      ? hasNecessaryPerm(ApiPermissionMap.CREATE_UNIVERSE)
                      : hasNecessaryPerm({
                          ...ApiPermissionMap.MODIFY_UNIVERSE,
                          onResource: universeUUID
                        });
                  }
                  // for edit mode , we need universe.update perm
                  return hasNecessaryPerm({
                    ...ApiPermissionMap.MODIFY_UNIVERSE,
                    onResource: universeUUID
                  });
                }}
                isControl
              >
                <YBButton
                  variant="primary"
                  size="large"
                  type="submit"
                  data-testid={`UniverseForm-${mode}-${clusterType}`}
                >
                  {submitLabel ? submitLabel : t('common.save')}
                </YBButton>
              </RbacValidator>
            </Box>
          </Grid>
        </Grid>
      </>
    );
  };

  const renderSections = () => {
    return (
      <>
        <CloudConfiguration runtimeConfigs={runtimeConfigs} />
        <InstanceConfiguration runtimeConfigs={runtimeConfigs} />
        {isPrimary && (
          <>
            <SecurityConfiguration runtimeConfigs={runtimeConfigs} />
            <AdvancedConfiguration runtimeConfigs={runtimeConfigs} />
          </>
        )}
        <GFlags runtimeConfigs={runtimeConfigs} />
        {isPrimary && <HelmOverrides />}
        <UserTags />
      </>
    );
  };

  //Form Context Values
  // const isPrimary = [clusterModes.NEW_PRIMARY, clusterModes.EDIT_PRIMARY].includes(mode);

  return (
    <Box className={classes.mainConatiner} data-testid="UniverseForm-Container">
      <FormProvider {...formMethods}>
        <form key={clusterType} onSubmit={formMethods.handleSubmit(onSubmit)}>
          <Box className={classes.formHeader}>{renderHeader()}</Box>
          <Box className={classes.formContainer}>{renderSections()}</Box>
          {!isViewMode && (
            <Box className={classes.formFooter} mt={4}>
              {renderFooter()}
            </Box>
          )}
        </form>
      </FormProvider>
    </Box>
  );
};
