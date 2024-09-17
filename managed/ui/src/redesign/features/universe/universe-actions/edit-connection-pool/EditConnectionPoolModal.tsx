import { FC } from 'react';
import { cloneDeep, get } from 'lodash';
import { toast } from 'react-toastify';
import { useMutation } from 'react-query';
import { useTranslation, Trans } from 'react-i18next';
import { useForm, Controller, FormProvider } from 'react-hook-form';
import { Box, makeStyles, Typography } from '@material-ui/core';
import { YBModal, YBToggleField, YBLabel, YBInput, YBTooltip } from '../../../../components';
import { getPrimaryCluster, createErrorMessage } from '../../universe-form/utils/helpers';
import { YSQLFormPayload } from '../edit-ysql-ycql/Helper';
import { api } from '../../../../utils/api';
import { Universe } from '../../universe-form/utils/dto';
import { DEFAULT_COMMUNICATION_PORTS } from '../../universe-form/utils/dto';
//RBAC
import { hasNecessaryPerm } from '../../../rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../rbac/ApiAndUserPermMapping';
import { RBAC_ERR_MSG_NO_PERM } from '../../../rbac/common/validator/ValidatorUtils';
//icons
import InfoMessageIcon from '../../../../assets/info-message.svg';
import InfoBlue from '../../../../assets/info-blue.svg';
import InfoError from '../../../../assets/info-red.svg';

const useStyles = makeStyles((theme) => ({
  toggleContainer: {
    display: 'flex',
    flexDirection: 'column',
    height: 'auto',
    width: '100%',
    rowGap: 8,
    backgroundColor: '#FCFCFC',
    border: '1px solid #E5E5E9',
    borderRadius: 8,
    padding: theme.spacing(2)
  },
  toggleRow: {
    display: 'flex',
    flexDirection: 'row',
    width: '100%',
    justifyContent: 'space-between',
    alignItems: 'flex-start'
  },
  overridePortsToggle: {
    display: 'flex',
    flexDirection: 'row',
    width: '100%',
    justifyContent: 'space-between',
    alignItems: 'center'
  },
  subText: {
    fontSize: 12,
    fontWeight: 400,
    color: '#67666C',
    marginTop: 4,
    lineHeight: '18px'
  },
  infoContainer: {
    display: 'flex',
    flexDirection: 'row',
    height: 'auto',
    width: '100%',
    padding: theme.spacing(1),
    backgroundColor: '#D7EFF4',
    borderRadius: 8,
    alignItems: 'flex-start',
    columnGap: '8px'
  },
  errorContainer: {
    display: 'flex',
    flexDirection: 'row',
    height: 'auto',
    width: '100%',
    padding: theme.spacing(1),
    backgroundColor: '#FDE2E2',
    borderRadius: 8,
    alignItems: 'flex-start',
    columnGap: '8px'
  },
  learnLink: {
    color: 'inherit',
    marginLeft: 24
  },
  uList: {
    listStyleType: 'disc',
    paddingLeft: '24px',
    marginBottom: 0
  }
}));

interface ConnectionPoolProps {
  open: boolean;
  onClose: () => void;
  universeData: Universe;
}

type ConnectionPoolFormValues = {
  enableConnectionPooling: boolean;
  overridePorts: boolean;
  ysqlServerRpcPort: number;
  internalYsqlServerRpcPort?: number;
};

const MAX_PORT = 65535;

export const EditConnectionPoolModal: FC<ConnectionPoolProps> = ({
  open,
  onClose,
  universeData
}) => {
  const { t } = useTranslation();
  const classes = useStyles();
  const { universeDetails, universeUUID } = universeData;
  const primaryCluster = cloneDeep(getPrimaryCluster(universeDetails));
  const isConPoolEnabledInitially = get(
    primaryCluster,
    'userIntent.enableConnectionPooling',
    false
  );
  const isYSQLAuthEnabled = get(primaryCluster, 'userIntent.enableYSQLAuth', false);
  const universeName = get(primaryCluster, 'userIntent.universeName');
  const currentRF = get(primaryCluster, 'userIntent.replicationFactor');

  const formMethods = useForm<ConnectionPoolFormValues>({
    defaultValues: {
      enableConnectionPooling: isConPoolEnabledInitially,
      overridePorts: false,
      ysqlServerRpcPort:
        universeDetails.communicationPorts.ysqlServerRpcPort ??
        DEFAULT_COMMUNICATION_PORTS.ysqlServerRpcPort,
      internalYsqlServerRpcPort:
        universeDetails.communicationPorts.internalYsqlServerRpcPort ??
        DEFAULT_COMMUNICATION_PORTS.internalYsqlServerRpcPort
    },
    mode: 'onTouched',
    reValidateMode: 'onChange'
  });
  const {
    control,
    watch,
    handleSubmit,
    formState: { isDirty }
  } = formMethods;

  const conPoolToggleValue = watch('enableConnectionPooling');
  const overRidePortsValue = watch('overridePorts');

  const YSQL_PORTS_LIST = [
    {
      id: 'ysqlServerRpcPort',
      tooltip: t('universeForm.advancedConfig.dbRPCPortTooltip')
    },
    {
      id: 'internalYsqlServerRpcPort',
      tooltip: t('universeForm.advancedConfig.ysqlConPortTooltip')
    }
  ];

  const updateYSQLSettings = useMutation(
    (values: YSQLFormPayload) => {
      return api.updateYSQLSettings(universeUUID, values);
    },
    {
      onSuccess: () => {
        toast.success(t('universeActions.editYSQLSettings.updateSettingsSuccessMsg'));
        onClose();
      },
      onError: (error) => {
        toast.error(createErrorMessage(error));
      }
    }
  );

  const handleFormSubmit = handleSubmit(async (values) => {
    try {
      let payload: YSQLFormPayload = {
        enableYSQL: true,
        enableYSQLAuth: isYSQLAuthEnabled,
        enableConnectionPooling: values.enableConnectionPooling,
        communicationPorts: {
          ysqlServerHttpPort: universeDetails.communicationPorts.ysqlServerHttpPort,
          ysqlServerRpcPort:
            values.overridePorts && values.ysqlServerRpcPort
              ? values.ysqlServerRpcPort
              : universeDetails.communicationPorts.ysqlServerRpcPort,
          ...(values.enableConnectionPooling && {
            internalYsqlServerRpcPort:
              values.overridePorts && values.internalYsqlServerRpcPort
                ? values.internalYsqlServerRpcPort
                : universeDetails.communicationPorts.internalYsqlServerRpcPort
          })
        }
      };
      await updateYSQLSettings.mutateAsync(payload);
    } catch (e) {
      console.log(e);
    }
  });

  const canUpdateYSQL = hasNecessaryPerm({
    onResource: universeUUID,
    ...ApiPermissionMap.UNIVERSE_CONFIGURE_YSQL
  });

  return (
    <YBModal
      open={open}
      title={t('universeActions.connectionPooling.modalTitle')}
      onClose={onClose}
      submitLabel={t('common.applyChanges')}
      cancelLabel={t('common.cancel')}
      overrideHeight={'600px'}
      overrideWidth={'600px'}
      submitTestId="EditConnectionPoolModal-Submit"
      cancelTestId="EditConnectionPoolModal-Cancel"
      onSubmit={handleFormSubmit}
      buttonProps={{
        primary: {
          disabled: !isDirty || !canUpdateYSQL
        }
      }}
      submitButtonTooltip={!canUpdateYSQL ? RBAC_ERR_MSG_NO_PERM : ''}
    >
      <FormProvider {...formMethods}>
        <Box
          display={'flex'}
          width="100%"
          height="100%"
          flexDirection="column"
          justifyContent={'space-between'}
          pt={2}
          pb={2}
          data-testid="EditConnectionPoolModal-Container"
        >
          <Box display="flex" flexDirection={'column'} width="100%">
            <Box className={classes.toggleContainer}>
              <Box className={classes.toggleRow}>
                <Box display={'flex'} flexDirection={'column'} width="453px">
                  <Typography variant="body1">
                    {t('universeActions.connectionPooling.toggleLabel')}
                  </Typography>
                  <Typography className={classes.subText}>
                    {t('universeActions.connectionPooling.subText')}
                  </Typography>
                </Box>
                <YBToggleField
                  control={control}
                  name="enableConnectionPooling"
                  inputProps={{
                    'data-testid': 'EditConnectionPoolModal-Toggle'
                  }}
                />
              </Box>
            </Box>
            {conPoolToggleValue && (
              <Box className={classes.toggleContainer} mt={2.5}>
                <Box className={classes.overridePortsToggle}>
                  <Typography variant="body1">
                    {t('universeActions.connectionPooling.portsOverride')}
                  </Typography>
                  <YBToggleField
                    control={control}
                    name="overridePorts"
                    inputProps={{
                      'data-testid': 'EditConnectionPoolModal-OverridePorts'
                    }}
                  />
                </Box>
                {overRidePortsValue && (
                  <>
                    {YSQL_PORTS_LIST.map((item) => (
                      <Controller
                        name={item.id}
                        render={({ field: { value, onChange } }) => {
                          return (
                            <Box
                              flex={1}
                              mt={1}
                              display={'flex'}
                              width="100%"
                              flexDirection={'row'}
                              alignItems={'center'}
                              key={item.id}
                            >
                              <Box flexShrink={1}>
                                <YBLabel dataTestId={`EditConnectionPoolModal-${item.id}`}>
                                  {t(`universeForm.advancedConfig.${item.id}`)} &nbsp;{' '}
                                  <YBTooltip title={item.tooltip}>
                                    <img alt="Info" src={InfoMessageIcon} />
                                  </YBTooltip>{' '}
                                </YBLabel>
                              </Box>

                              <Box flex={1} display={'flex'} width="300px">
                                <YBInput
                                  value={value}
                                  onChange={onChange}
                                  onBlur={(event) => {
                                    let port =
                                      Number(event.target.value.replace(/\D/g, '')) ||
                                      get(universeDetails.communicationPorts, item.id);
                                    port = port > MAX_PORT ? MAX_PORT : port;
                                    onChange(port);
                                  }}
                                  inputProps={{
                                    'data-testid': `EditConnectionPoolModal-Input-${item.id}}`
                                  }}
                                />
                              </Box>
                            </Box>
                          );
                        }}
                      />
                    ))}
                  </>
                )}
              </Box>
            )}
          </Box>
          <Box display="flex" flexDirection={'column'} width="100%">
            {/* Enabling Connection Pooling */}
            {!isConPoolEnabledInitially && conPoolToggleValue && (
              <Box
                mt={2}
                className={currentRF >= 3 ? classes.infoContainer : classes.errorContainer}
              >
                <img src={currentRF >= 3 ? InfoBlue : InfoError} alt="--" />
                <Typography variant="body2">
                  <Trans
                    i18nKey={
                      currentRF >= 3
                        ? 'universeActions.connectionPooling.enableWarningRF3'
                        : 'universeActions.connectionPooling.enableWarningRF1'
                    }
                    values={{ universeName }}
                  />
                </Typography>
              </Box>
            )}
            {/* Disabling Connection Pooling */}
            {isConPoolEnabledInitially && !conPoolToggleValue && (
              <Box
                mt={2}
                className={currentRF >= 3 ? classes.infoContainer : classes.errorContainer}
              >
                <img src={currentRF >= 3 ? InfoBlue : InfoError} alt="--" />
                {currentRF >= 3 ? (
                  <Typography variant="body2">
                    <Trans i18nKey={'universeActions.connectionPooling.disableWarning1RF3'} />{' '}
                    <br />
                    <ul className={classes.uList}>
                      <li>
                        <Trans
                          i18nKey={'universeActions.connectionPooling.disableWarning2RF3'}
                          values={{ universeName }}
                        />
                      </li>
                      <li>
                        <Trans i18nKey={'universeActions.connectionPooling.disableWarning3RF3'} />
                      </li>
                    </ul>
                  </Typography>
                ) : (
                  <Typography variant="body2">
                    <Trans
                      i18nKey={'universeActions.connectionPooling.disableWarning1RF1'}
                      values={{ universeName }}
                    />
                    <br />
                    <br />
                    <Trans i18nKey={'universeActions.connectionPooling.disableWarning2RF1'} />
                  </Typography>
                )}
              </Box>
            )}
            {isConPoolEnabledInitially === conPoolToggleValue && overRidePortsValue && (
              <Box
                mt={2}
                className={currentRF >= 3 ? classes.infoContainer : classes.errorContainer}
              >
                <img src={currentRF >= 3 ? InfoBlue : InfoError} alt="--" />
                <Typography variant="body2">
                  <Trans
                    i18nKey={
                      currentRF >= 3
                        ? 'universeActions.connectionPooling.overrideWarningRF3'
                        : 'universeActions.connectionPooling.overrideWarningRF1'
                    }
                    values={{ universeName }}
                  />
                </Typography>
              </Box>
            )}
          </Box>
        </Box>
      </FormProvider>
    </YBModal>
  );
};
