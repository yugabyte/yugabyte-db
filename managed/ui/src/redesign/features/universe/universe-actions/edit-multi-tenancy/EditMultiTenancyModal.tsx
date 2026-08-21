import { FC } from 'react';
import { Box, makeStyles, Typography } from '@material-ui/core';
import { AxiosError } from 'axios';
import { get } from 'lodash';
import { Controller, FormProvider, useForm } from 'react-hook-form';
import { Trans, useTranslation } from 'react-i18next';
import { useMutation } from 'react-query';
import { toast } from 'react-toastify';

import { handleServerError } from '@app/utils/errorHandlingUtils';
import { YBInput, YBModal, YBToggleField } from '../../../../components';
import { api } from '../../../../utils/api';
import { ApiPermissionMap } from '../../../rbac/ApiAndUserPermMapping';
import { hasNecessaryPerm } from '../../../rbac/common/RbacApiPermValidator';
import { RBAC_ERR_MSG_NO_PERM } from '../../../rbac/common/validator/ValidatorUtils';
import { Universe } from '../../universe-form/utils/dto';
import { getPrimaryCluster } from '../../universe-form/utils/helpers';
import { YSQLFormPayload } from '../edit-ysql-ycql/Helper';
import {
  clampMultiTenancyQosMaxDbCpuPercent,
  clampMultiTenancyQosMaxDbCount,
  MULTI_TENANCY_QOS_MAX_DB_CPU_PERCENT_FALLBACK,
  MULTI_TENANCY_QOS_MAX_DB_CPU_PERCENT_MAX,
  MULTI_TENANCY_QOS_MAX_DB_CPU_PERCENT_MIN,
  MULTI_TENANCY_QOS_MAX_DB_CPU_PERCENT_STEP,
  MULTI_TENANCY_QOS_MAX_DB_COUNT_FALLBACK,
  MULTI_TENANCY_QOS_MAX_DB_COUNT_MIN,
  MULTI_TENANCY_QOS_MAX_DB_COUNT_STEP
} from './multiTenancyQosDbCount';

import InfoBlue from '../../../../assets/info-blue.svg';
import InfoError from '../../../../assets/info-red.svg';


const useStyles = makeStyles((theme) => ({
  toggleContainer: {
    display: 'flex',
    flexDirection: 'column',
    height: 'auto',
    width: '100%',
    rowGap: theme.spacing(1),
    backgroundColor: '#FCFCFC',
    border: '1px solid #E5E5E9',
    borderRadius: theme.shape.borderRadius,
    padding: theme.spacing(2)
  },
  toggleRow: {
    display: 'flex',
    flexDirection: 'row',
    width: '100%',
    justifyContent: 'space-between',
    alignItems: 'flex-start'
  },
  subText: {
    fontSize: 12,
    fontWeight: 400,
    color: '#67666C',
    marginTop: theme.spacing(0.5),
    lineHeight: '18px'
  },
  infoContainer: {
    display: 'flex',
    flexDirection: 'row',
    height: 'auto',
    width: '100%',
    padding: theme.spacing(1),
    backgroundColor: '#D7EFF4',
    borderRadius: theme.shape.borderRadius,
    alignItems: 'flex-start',
    columnGap: theme.spacing(1)
  },
  errorContainer: {
    display: 'flex',
    flexDirection: 'row',
    height: 'auto',
    width: '100%',
    padding: theme.spacing(1),
    backgroundColor: '#FDE2E2',
    borderRadius: theme.shape.borderRadius,
    alignItems: 'flex-start',
    columnGap: theme.spacing(1)
  },
  qosFieldsColumn: {
    display: 'flex',
    flexDirection: 'column',
    marginTop: theme.spacing(2),
    maxWidth: 280,
    rowGap: theme.spacing(2)
  }
}));

interface EditMultiTenancyModalProps {
  open: boolean;
  onClose: () => void;
  universeData: Universe;
}

type MultiTenancyFormValues = {
  enableQos: boolean;
  qosMaxDbCpuPercent: number | '';
  qosMaxDbCount: number | '';
};

function getMultiTenancyFormDefaults(
  universeDetails: Universe['universeDetails']
): MultiTenancyFormValues {
  const primaryCluster = getPrimaryCluster(universeDetails);
  if (!primaryCluster) {
    return {
      enableQos: false,
      qosMaxDbCpuPercent: MULTI_TENANCY_QOS_MAX_DB_CPU_PERCENT_FALLBACK,
      qosMaxDbCount: MULTI_TENANCY_QOS_MAX_DB_COUNT_FALLBACK
    };
  }
  const initialEnableQos = get(primaryCluster, 'userIntent.multiTenancy.enableQos', false);
  const initialCpuRaw = get(primaryCluster, 'userIntent.multiTenancy.qosMaxDbCpuPercent');
  const initialCpuPercent =
    typeof initialCpuRaw === 'number' && !Number.isNaN(initialCpuRaw)
      ? initialCpuRaw
      : MULTI_TENANCY_QOS_MAX_DB_CPU_PERCENT_FALLBACK;
  const initialDbCountRaw = get(primaryCluster, 'userIntent.multiTenancy.qosMaxDbCount');
  const initialDbCount =
    typeof initialDbCountRaw === 'number' && !Number.isNaN(initialDbCountRaw)
      ? initialDbCountRaw
      : MULTI_TENANCY_QOS_MAX_DB_COUNT_FALLBACK;
  return {
    enableQos: initialEnableQos,
    qosMaxDbCpuPercent: initialCpuPercent,
    qosMaxDbCount: initialDbCount
  };
}

/** Renders only while open so react-hook-form defaultValues match universeData for that open. */
const EditMultiTenancyModalLoaded: FC<{
  onClose: () => void;
  universeData: Universe;
}> = ({ onClose, universeData }) => {
  const { t } = useTranslation();
  const classes = useStyles();
  const { universeDetails, universeUUID } = universeData;
  const primaryClusterForMeta = getPrimaryCluster(universeDetails);
  const isYSQLAuthEnabled = get(primaryClusterForMeta, 'userIntent.enableYSQLAuth', false);
  const universeName = get(primaryClusterForMeta, 'userIntent.universeName');
  const currentRF = get(primaryClusterForMeta, 'userIntent.replicationFactor');
  const isConPoolEnabled = get(primaryClusterForMeta, 'userIntent.enableConnectionPooling', false);

  const formMethods = useForm<MultiTenancyFormValues>({
    defaultValues: getMultiTenancyFormDefaults(universeDetails),
    mode: 'onTouched',
    reValidateMode: 'onChange'
  });

  const {
    control,
    watch,
    handleSubmit,
    formState: { isDirty }
  } = formMethods;

  const enableQosValue = watch('enableQos');

  const buildCommunicationPorts = () => ({
    ysqlServerHttpPort: universeDetails.communicationPorts.ysqlServerHttpPort,
    ysqlServerRpcPort: universeDetails.communicationPorts.ysqlServerRpcPort,
    ...(isConPoolEnabled && {
      internalYsqlServerRpcPort: universeDetails.communicationPorts.internalYsqlServerRpcPort
    })
  });

  const updateYSQLSettingsMutation = useMutation(
    (payload: YSQLFormPayload) => api.updateYSQLSettings(universeUUID, payload),
    {
      onSuccess: () => {
        toast.success(t('universeActions.editYSQLSettings.updateSettingsSuccessMsg'));
        onClose();
      },
      onError: (error: Error | AxiosError) => {
        handleServerError(error, {
          customErrorLabel: t('universeActions.multiTenancy.requestFailedLabel')
        });
      }
    }
  );

  const handleFormSubmit = handleSubmit((values) => {
    const payload: YSQLFormPayload = {
      enableYSQL: true,
      enableYSQLAuth: isYSQLAuthEnabled,
      enableConnectionPooling: isConPoolEnabled,
      communicationPorts: buildCommunicationPorts(),
      multiTenancy: {
        enableQos: values.enableQos,
        ...(values.enableQos && {
          qosMaxDbCpuPercent: clampMultiTenancyQosMaxDbCpuPercent(values.qosMaxDbCpuPercent),
          qosMaxDbCount: clampMultiTenancyQosMaxDbCount(values.qosMaxDbCount)
        })
      }
    };
    updateYSQLSettingsMutation.mutate(payload);
  });

  const hasConfigureYsqlPerms = hasNecessaryPerm({
    onResource: universeUUID,
    ...ApiPermissionMap.UNIVERSE_CONFIGURE_YSQL
  });

  return (
    <YBModal
      open
      title={t('universeActions.multiTenancy.modalTitle')}
      onClose={onClose}
      submitLabel={t('common.applyChanges')}
      cancelLabel={t('common.cancel')}
      overrideHeight={'560px'}
      overrideWidth={'560px'}
      submitTestId="EditMultiTenancyModal-Submit"
      cancelTestId="EditMultiTenancyModal-Cancel"
      onSubmit={handleFormSubmit}
      buttonProps={{
        primary: {
          disabled: !isDirty || !hasConfigureYsqlPerms
        }
      }}
      submitButtonTooltip={!hasConfigureYsqlPerms ? RBAC_ERR_MSG_NO_PERM : ''}
    >
      <FormProvider {...formMethods}>
        <Box
          display="flex"
          width="100%"
          height="100%"
          flexDirection="column"
          justifyContent="space-between"
          pt={2}
          pb={2}
          data-testid="EditMultiTenancyModal-Container"
        >
          <Box display="flex" flexDirection="column" width="100%">
            <Box className={classes.toggleContainer}>
              <Box className={classes.toggleRow}>
                <Box display="flex" flexDirection="column" maxWidth={400}>
                  <Typography variant="body1">
                    {t('universeActions.multiTenancy.toggleLabel')}
                  </Typography>
                  <Typography className={classes.subText}>
                    {t('universeActions.multiTenancy.subText')}
                  </Typography>
                </Box>
                <YBToggleField
                  control={control}
                  name="enableQos"
                  inputProps={{
                    'data-testid': 'EditMultiTenancyModal-Toggle'
                  }}
                />
              </Box>
            </Box>
            {enableQosValue && (
              <Box className={classes.qosFieldsColumn}>
                <Controller
                  name="qosMaxDbCpuPercent"
                  control={control}
                  render={({ field }) => (
                    <YBInput
                      type="number"
                      fullWidth
                      trimWhitespace={false}
                      label={t('universeActions.multiTenancy.cpuPercentLabel')}
                      helperText={t('universeActions.multiTenancy.cpuPercentHelper')}
                      value={
                        field.value === undefined || field.value === null ? '' : String(field.value)
                      }
                      inputProps={{
                        min: MULTI_TENANCY_QOS_MAX_DB_CPU_PERCENT_MIN,
                        max: MULTI_TENANCY_QOS_MAX_DB_CPU_PERCENT_MAX,
                        step: MULTI_TENANCY_QOS_MAX_DB_CPU_PERCENT_STEP,
                        'data-testid': 'EditMultiTenancyModal-QosMaxDbCpuPercent'
                      }}
                      onChange={(event) => {
                        const raw = event.target.value;
                        if (raw === '') {
                          field.onChange('');
                          return;
                        }
                        const parsed = parseFloat(raw);
                        field.onChange(Number.isFinite(parsed) ? parsed : '');
                      }}
                      onBlur={(event) => {
                        field.onBlur();
                        field.onChange(
                          clampMultiTenancyQosMaxDbCpuPercent(
                            event.target.value === ''
                              ? MULTI_TENANCY_QOS_MAX_DB_CPU_PERCENT_FALLBACK
                              : event.target.value
                          )
                        );
                      }}
                    />
                  )}
                />
                <Controller
                  name="qosMaxDbCount"
                  control={control}
                  render={({ field }) => (
                    <YBInput
                      type="number"
                      fullWidth
                      trimWhitespace={false}
                      label={t('universeActions.multiTenancy.dbCountLabel')}
                      helperText={t('universeActions.multiTenancy.dbCountHelper')}
                      value={
                        field.value === undefined || field.value === null ? '' : String(field.value)
                      }
                      inputProps={{
                        min: MULTI_TENANCY_QOS_MAX_DB_COUNT_MIN,
                        step: MULTI_TENANCY_QOS_MAX_DB_COUNT_STEP,
                        'data-testid': 'EditMultiTenancyModal-QosMaxDbCount'
                      }}
                      onChange={(event) => {
                        const raw = event.target.value;
                        if (raw === '') {
                          field.onChange('');
                          return;
                        }
                        const parsed = parseInt(raw, 10);
                        field.onChange(Number.isFinite(parsed) ? parsed : '');
                      }}
                      onBlur={(event) => {
                        field.onBlur();
                        field.onChange(
                          clampMultiTenancyQosMaxDbCount(
                            event.target.value === ''
                              ? MULTI_TENANCY_QOS_MAX_DB_COUNT_FALLBACK
                              : event.target.value
                          )
                        );
                      }}
                    />
                  )}
                />
              </Box>
            )}
          </Box>
          {isDirty && (
            <Box mt={2} className={currentRF >= 3 ? classes.infoContainer : classes.errorContainer}>
              {currentRF >= 3 ? <InfoBlue /> : <InfoError />}
              <Typography variant="body2">
                <Trans
                  i18nKey={
                    currentRF >= 3
                      ? 'universeActions.multiTenancy.changeWarningRf3'
                      : 'universeActions.multiTenancy.changeWarningRf1'
                  }
                  values={{ universeName }}
                />
              </Typography>
            </Box>
          )}
        </Box>
      </FormProvider>
    </YBModal>
  );
};

export const EditMultiTenancyModal: FC<EditMultiTenancyModalProps> = ({
  open,
  onClose,
  universeData
}) => {
  if (!open) {
    return null;
  }
  return <EditMultiTenancyModalLoaded onClose={onClose} universeData={universeData} />;
};

export {
  clampMultiTenancyQosMaxDbCpuPercent,
  clampMultiTenancyQosMaxDbCount,
  MULTI_TENANCY_QOS_MAX_DB_CPU_PERCENT_FALLBACK,
  MULTI_TENANCY_QOS_MAX_DB_CPU_PERCENT_MAX,
  MULTI_TENANCY_QOS_MAX_DB_CPU_PERCENT_MIN,
  MULTI_TENANCY_QOS_MAX_DB_CPU_PERCENT_STEP,
  MULTI_TENANCY_QOS_MAX_DB_COUNT_FALLBACK,
  MULTI_TENANCY_QOS_MAX_DB_COUNT_MIN,
  MULTI_TENANCY_QOS_MAX_DB_COUNT_STEP
} from './multiTenancyQosDbCount';
