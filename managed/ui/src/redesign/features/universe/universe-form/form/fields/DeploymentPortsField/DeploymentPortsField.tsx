import { FC } from 'react';
import _ from 'lodash';
import { useEffectOnce } from 'react-use';
import { useTranslation } from 'react-i18next';
import { Controller, useWatch, useFormContext } from 'react-hook-form';
import { Box, Grid, makeStyles, Typography } from '@material-ui/core';
import { YBInput, YBToggleField, YBLabel, YBTooltip } from '../../../../../../components';
import {
  CloudType,
  DEFAULT_COMMUNICATION_PORTS,
  UniverseFormData,
  CommunicationPorts
} from '../../../utils/dto';
import {
  COMMUNICATION_PORTS_FIELD,
  YCQL_FIELD,
  YSQL_FIELD,
  YEDIS_FIELD,
  CUSTOMIZE_PORT_FIELD,
  PROVIDER_FIELD,
  CONNECTION_POOLING_FIELD
} from '../../../utils/constants';
import { useFormFieldStyles } from '../../../universeMainStyle';
//icons
import InfoMessageIcon from '../../../../../../assets/info-message.svg';

interface DeploymentPortsFieldids {
  disabled: boolean;
  isEditMode?: boolean;
}

const MAX_PORT = 65535;

const useStyles = makeStyles((theme) => ({
  portsContainer: {
    height: 'auto',
    width: 900,
    border: '1px dotted #E5E5E9',
    borderRadius: '8px'
  },
  portInnerContainer: {
    padding: theme.spacing(2),
    borderBottom: '1px solid #E5E5E9'
  },
  portInnerWithoutBorder: {
    padding: theme.spacing(2)
  },
  portHeader: {
    fontSize: 11.5,
    fontWeight: 500,
    color: '#67666C'
  }
}));

export const DeploymentPortsField: FC<DeploymentPortsFieldids> = ({ disabled, isEditMode }) => {
  const { control, getValues, setValue } = useFormContext<UniverseFormData>();
  const { t } = useTranslation();
  const classes = useFormFieldStyles();
  const localClasses = useStyles();

  //watchers
  const ysqlEnabled = useWatch({ name: YSQL_FIELD });
  const ycqlEnabled = useWatch({ name: YCQL_FIELD });
  const yedisEnabled = useWatch({ name: YEDIS_FIELD });
  const connectionPoolingEnabled = useWatch({ name: CONNECTION_POOLING_FIELD });
  const provider = useWatch({ name: PROVIDER_FIELD });
  const customizePort = useWatch({ name: CUSTOMIZE_PORT_FIELD });

  // Master and TServer ports can be modified in EDIT mode when the provider is not Kubernetes.
  const MASTER_TSERVER_PORTS = [
    { id: 'masterHttpPort', visible: true, disabled: disabled },
    { id: 'masterRpcPort', visible: true, disabled: disabled },
    { id: 'tserverHttpPort', visible: true, disabled: disabled },
    { id: 'tserverRpcPort', visible: true, disabled: disabled }
  ].filter((ports) => ports.visible);

  // Redis, YCQL, YSQL ports cannot be modified in EDIT mode but can be edited via Universe actions in Overview section.
  const YCQL_PORTS = [
    {
      id: 'yqlServerHttpPort',
      visible: ycqlEnabled,
      disabled: isEditMode
    },
    {
      id: 'yqlServerRpcPort',
      visible: ycqlEnabled,
      disabled: isEditMode,
      tooltip: t('universeForm.advancedConfig.dbRPCPortTooltip')
    }
  ].filter((ports) => ports.visible);

  const YSQL_PORTS = [
    { id: 'ysqlServerHttpPort', visible: ysqlEnabled, disabled: isEditMode },
    {
      id: 'ysqlServerRpcPort',
      visible: ysqlEnabled,
      disabled: isEditMode,
      tooltip: t('universeForm.advancedConfig.dbRPCPortTooltip')
    },
    {
      id: 'internalYsqlServerRpcPort',
      visible: ysqlEnabled && connectionPoolingEnabled,
      disabled: isEditMode,
      tooltip: t('universeForm.advancedConfig.ysqlConPortTooltip')
    }
  ].filter((ports) => ports.visible);

  const OTHER_PORTS = [
    { id: 'redisServerHttpPort', visible: yedisEnabled, disabled: isEditMode },
    { id: 'redisServerRpcPort', visible: yedisEnabled, disabled: isEditMode },
    { id: 'nodeExporterPort', visible: provider?.code !== CloudType.onprem, disabled: disabled }
  ];

  const PORT_GROUPS = [
    {
      name: 'MASTER & TSERVER',
      PORTS_LIST: MASTER_TSERVER_PORTS,
      visible: MASTER_TSERVER_PORTS.length > 0
    },
    {
      name: 'YSQL',
      PORTS_LIST: YSQL_PORTS,
      visible: YSQL_PORTS.length > 0
    },
    {
      name: 'YCQL',
      PORTS_LIST: YCQL_PORTS,
      visible: YCQL_PORTS.length > 0
    },
    {
      name: 'OTHERS',
      PORTS_LIST: OTHER_PORTS,
      visible: OTHER_PORTS.length > 0
    }
  ].filter((pg) => pg.visible);

  const portsConfig = [...MASTER_TSERVER_PORTS, ...YSQL_PORTS, ...YCQL_PORTS, ...OTHER_PORTS];

  const isPortsCustomized = (communicationPorts: CommunicationPorts) => {
    return portsConfig.reduce(
      (acc, current) =>
        acc || DEFAULT_COMMUNICATION_PORTS[current.id] !== communicationPorts[current.id],
      false
    );
  };

  useEffectOnce(() => {
    if (isEditMode && isPortsCustomized(getValues(COMMUNICATION_PORTS_FIELD))) {
      setValue(CUSTOMIZE_PORT_FIELD, true);
    }
  });

  return (
    <Controller
      name={COMMUNICATION_PORTS_FIELD}
      render={({ field: { value, onChange } }) => {
        return (
          <Box display="flex" alignItems="flex-start" data-testid="DeploymentPortsField-Container">
            <YBToggleField
              name={CUSTOMIZE_PORT_FIELD}
              inputProps={{
                'data-testid': 'DeploymentPortsField-CoustomizePortToggle'
              }}
              control={control}
              disabled={disabled}
            />
            <Box flex={1} display="flex" flexDirection="column">
              <YBLabel
                dataTestId="DeploymentPortsField-Label"
                className={classes.advancedConfigLabel}
              >
                {t('universeForm.advancedConfig.overridePorts')}
              </YBLabel>
              {customizePort && (
                <Box
                  display="flex"
                  flexDirection={'column'}
                  mt={3}
                  className={localClasses.portsContainer}
                >
                  {PORT_GROUPS.map((pg, i) => (
                    <Box
                      className={
                        i === PORT_GROUPS.length - 1
                          ? localClasses.portInnerWithoutBorder
                          : localClasses.portInnerContainer
                      }
                      key={i}
                    >
                      <Typography className={localClasses.portHeader}>{pg.name}</Typography>
                      <Grid container>
                        {pg.PORTS_LIST.map((item) => (
                          <Grid item lg={6} key={item.id}>
                            <Box display="flex" mr={4} mt={1}>
                              <YBLabel dataTestId={`DeploymentPortsField-${item.id}`}>
                                {t(`universeForm.advancedConfig.${item.id}`)}
                                &nbsp;
                                <YBTooltip title={_.get(item, 'tooltip', '')}>
                                  <img alt="Info" src={InfoMessageIcon} />
                                </YBTooltip>
                              </YBLabel>
                              <Box flex={1}>
                                <YBInput
                                  disabled={item.disabled}
                                  className={
                                    Number(value[item.id]) ===
                                    Number(DEFAULT_COMMUNICATION_PORTS[item.id])
                                      ? ''
                                      : 'communication-ports-editor__overridden-value'
                                  }
                                  value={value[item.id]}
                                  onChange={(event) =>
                                    onChange({ ...value, [item.id]: event.target.value })
                                  }
                                  onBlur={(event) => {
                                    let port =
                                      Number(event.target.value.replace(/\D/g, '')) ||
                                      DEFAULT_COMMUNICATION_PORTS[item.id];
                                    port = port > MAX_PORT ? MAX_PORT : port;
                                    onChange({ ...value, [item.id]: port });
                                  }}
                                  inputProps={{
                                    'data-testid': `DeploymentPortsField-Input${item.id}`
                                  }}
                                />
                              </Box>
                            </Box>
                          </Grid>
                        ))}
                      </Grid>
                    </Box>
                  ))}
                </Box>
              )}
            </Box>
          </Box>
        );
      }}
    />
  );
};

//hidden for k8s
