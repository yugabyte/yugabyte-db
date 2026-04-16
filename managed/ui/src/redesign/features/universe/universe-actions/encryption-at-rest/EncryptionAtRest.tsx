import { FC } from 'react';
import clsx from 'clsx';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { useForm, FormProvider } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { Box, Divider, Typography } from '@material-ui/core';
import { toast } from 'react-toastify';
import { YBCheckboxField, YBModal, YBToggleField, YBTooltip } from '../../../../components';
import {
  getLastRotationDetails,
  EncryptionAtRestFormValues,
  useMKRStyles,
  KMS_FIELD_NAME,
  UNIVERSE_KEY_FIELD_NAME,
  EAR_FIELD_NAME,
  TOAST_AUTO_DISMISS_INTERVAL
} from './EncryptionAtRestUtils';
import { KMSField } from './components/KMSField';
import { RotationHistory } from './components/RotationHistory';
import { api, QUERY_KEY } from '../../../../utils/api';
import { EncryptionAtRestConfig, Universe, KmsConfig } from '../../universe-form/utils/dto';
import { YBLoading } from '../../../../../components/common/indicators';
import { hasNecessaryPerm } from '../../../rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../rbac/ApiAndUserPermMapping';
import { RBAC_ERR_MSG_NO_PERM } from '../../../rbac/common/validator/ValidatorUtils';

//EAR Component
interface EncryptionAtRestProps {
  open: boolean;
  onClose: () => void;
  universeDetails: Universe;
}

const TOAST_OPTIONS = { autoClose: TOAST_AUTO_DISMISS_INTERVAL };

export const EncryptionAtRest: FC<EncryptionAtRestProps> = ({ open, onClose, universeDetails }) => {
  const { t } = useTranslation();
  const queryClient = useQueryClient();
  const classes = useMKRStyles();

  //universe current status
  const {
    universeDetails: { encryptionAtRestConfig }
  } = universeDetails;
  const universeId = universeDetails.universeUUID;

  //fetch kms configs
  const { data: kmsConfigs = [], isLoading: isKMSConfigsLoading } = useQuery(
    QUERY_KEY.getKMSConfigs,
    api.getKMSConfigs
  );
  //fetch kms history
  const {
    data: kmsHistory = [],
    isLoading: isKMSHistoryLoading
  } = useQuery(QUERY_KEY.getKMSHistory, () => api.getKMSHistory(universeId));

  //kms info
  const { encryptionAtRestEnabled, kmsConfigUUID } = encryptionAtRestConfig;
  const activeKMSConfig = kmsConfigs.find(
    (config: KmsConfig) => config.metadata.configUUID === kmsConfigUUID
  );

  //init form
  const formMethods = useForm<EncryptionAtRestFormValues>({
    defaultValues: {
      encryptionAtRestEnabled,
      kmsConfigUUID: kmsConfigUUID ?? '',
      rotateUniverseKey: false
    },
    mode: 'onChange',
    reValidateMode: 'onChange'
  });
  const { control, watch, handleSubmit } = formMethods;

  //watch field values
  const earToggleEnabled = watch(EAR_FIELD_NAME);
  const currentKmsConfigUUID = watch(KMS_FIELD_NAME);
  const rotateUniverseKey = watch(UNIVERSE_KEY_FIELD_NAME); //Universe key is rotated
  const rotateMasterKey = encryptionAtRestEnabled && currentKmsConfigUUID !== kmsConfigUUID; //Master Key is rotated
  const rotationInfo = getLastRotationDetails(kmsHistory ?? [], kmsConfigs);

  //Enable Form
  const isFormValid = () => {
    if (!encryptionAtRestEnabled && !earToggleEnabled) return false;

    return (
      rotateUniverseKey ||
      encryptionAtRestEnabled != earToggleEnabled ||
      currentKmsConfigUUID != kmsConfigUUID
    );
  };

  //methods
  const setKMSConfig = useMutation(
    (values: EncryptionAtRestConfig) => {
      return api.setKMSConfig(universeId, values);
    },
    {
      onSuccess: () => {
        void queryClient.invalidateQueries(QUERY_KEY.getKMSHistory);

        if (earToggleEnabled) {
          //enabling kms for the first time
          !encryptionAtRestEnabled &&
            toast.success(t('universeActions.encryptionAtRest.earEnabledSuccess'), TOAST_OPTIONS);

          //rotating universe or master key
          encryptionAtRestEnabled &&
            toast.success(
              rotateUniverseKey
                ? t('universeActions.encryptionAtRest.universeKeyRotationSuccess')
                : t('universeActions.encryptionAtRest.masterKeyRotationSuccess'),
              TOAST_OPTIONS
            );
        } else {
          //disabling kms
          toast.warn(t('universeActions.encryptionAtRest.earDisabedSuccess'), TOAST_OPTIONS);
        }

        onClose();
      },
      onError: () => {
        toast.error(t('common.genericFailure'), TOAST_OPTIONS);
      }
    }
  );

  const handleFormSubmit = handleSubmit(async (values) => {
    const payload: EncryptionAtRestConfig = {
      key_op: values.encryptionAtRestEnabled ? 'ENABLE' : 'DISABLE',
      kmsConfigUUID: values.kmsConfigUUID
    };
    try {
      await setKMSConfig.mutateAsync(payload);
    } catch (e) {
      console.error(e);
    }
  });

  const canSetKey = hasNecessaryPerm({
    onResource: universeId,
    ...ApiPermissionMap.SET_ENCRYPTION_KEY
  });

  if (isKMSConfigsLoading || isKMSHistoryLoading) return <YBLoading />;

  return (
    <YBModal
      open={open}
      titleSeparator
      size="sm"
      overrideHeight="auto"
      cancelLabel={t('common.cancel')}
      submitLabel={t('common.apply')}
      title={t('universeActions.encryptionAtRest.title')}
      onClose={onClose}
      onSubmit={handleFormSubmit}
      submitTestId="EncryptionAtRest-Submit"
      cancelTestId="EncryptionAtRest-Close"
      buttonProps={{
        primary: {
          disabled: !isFormValid() || !canSetKey
        }
      }}
      submitButtonTooltip={!canSetKey ? RBAC_ERR_MSG_NO_PERM : ''}
    >
      <FormProvider {...formMethods}>
        <Box
          mb={4}
          mt={2}
          display="flex"
          width="100%"
          flexDirection="column"
          data-testid="EncryptionAtRest-Modal"
        >
          <Box
            display="flex"
            flexDirection="column"
            className={clsx(classes.enableEARContainer, classes.container)}
          >
            <Box display="flex" alignItems="center" justifyContent="space-between">
              <Box>
                <Typography variant="h6">
                  {t('universeActions.encryptionAtRest.enableEAR')}
                </Typography>
              </Box>
              <Box>
                <YBToggleField
                  name={EAR_FIELD_NAME}
                  inputProps={{
                    'data-testid': 'EnableEncryptionAtRest-Toggle'
                  }}
                  control={control}
                />
              </Box>
            </Box>
            {!encryptionAtRestEnabled && !earToggleEnabled && kmsHistory.length > 1 && (
              <RotationHistory
                rotationInfo={rotationInfo.lastActiveKey}
                lastActiveKMS={rotationInfo?.lastActiveKey?.configName}
              />
            )}

            {/* Enabling EAR for the first time */}
            {kmsHistory.length < 1 && earToggleEnabled && (
              <Box mt={2}>
                <KMSField
                  disabled={rotateUniverseKey}
                  label={t('universeActions.encryptionAtRest.selectKMSConfig')}
                />
              </Box>
            )}
          </Box>
          {/* Enabling EAR for the first time */}

          {/* ------------------------------------------------------------------------------- */}

          {/* Rotating universe or master key */}
          {kmsHistory.length >= 1 && earToggleEnabled && (
            <Box mt={4} display="flex" flexDirection="column" className={classes.container}>
              <Box className={classes.subContainer}>
                <Box>
                  <Typography variant="h6">
                    {t('universeActions.encryptionAtRest.kmsConfig')}
                  </Typography>
                </Box>
                <Box mt={1}>
                  <YBTooltip
                    title={clsx(
                      rotateUniverseKey && t('universeActions.encryptionAtRest.rotateBothWarning')
                    )}
                    placement="top"
                  >
                    <div>
                      <KMSField
                        disabled={rotateUniverseKey}
                        label={
                          encryptionAtRestEnabled
                            ? t('universeActions.encryptionAtRest.rotateKMSConfig')
                            : t('universeActions.encryptionAtRest.selectKMSConfig')
                        }
                        activeKMS={encryptionAtRestEnabled ? kmsConfigUUID : ''}
                      />
                    </div>
                  </YBTooltip>
                  <RotationHistory
                    rotationInfo={rotationInfo.masterKey}
                    lastActiveKMS={!encryptionAtRestEnabled ? activeKMSConfig?.metadata?.name : ''}
                  />
                </Box>
              </Box>

              {encryptionAtRestEnabled && (
                <>
                  <Divider />

                  <Box className={classes.subContainer}>
                    <Box>
                      <Typography variant="h6">
                        {t('universeActions.encryptionAtRest.universeKey')}
                      </Typography>
                    </Box>
                    <Box mt={1} className={clsx(classes.container, classes.universeKeyContainer)}>
                      <YBTooltip
                        title={clsx(
                          rotateMasterKey && t('universeActions.encryptionAtRest.rotateBothWarning')
                        )}
                        placement="top"
                      >
                        <div>
                          <YBCheckboxField
                            name={UNIVERSE_KEY_FIELD_NAME}
                            label={t('universeActions.encryptionAtRest.rotateUniverseKey')}
                            control={control}
                            inputProps={{
                              'data-testid': 'RotateUniverseKey-Checkbox'
                            }}
                            disabled={rotateMasterKey}
                          />
                        </div>
                      </YBTooltip>
                    </Box>
                    <RotationHistory rotationInfo={rotationInfo.universeKey} />
                  </Box>
                </>
              )}
            </Box>
          )}
          {/* Rotating universe key or master key */}
        </Box>
      </FormProvider>
    </YBModal>
  );
};
