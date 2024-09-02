import { FC } from 'react';
import _ from 'lodash';
import { useEffectOnce } from 'react-use';
import { useTranslation } from 'react-i18next';
import { Controller, useWatch, useFormContext } from 'react-hook-form';
import { Box, Grid } from '@material-ui/core';
import { YBInput, YBToggleField, YBLabel } from '../../../../../../components';
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
  PROVIDER_FIELD
} from '../../../utils/constants';
import { useFormFieldStyles } from '../../../universeMainStyle';

interface DeploymentPortsFieldids {
  disabled: boolean;
  isEditMode?: boolean;
}

const MAX_PORT = 65535;

export const DeploymentPortsField: FC<DeploymentPortsFieldids> = ({ disabled, isEditMode }) => {
  const { control, getValues, setValue } = useFormContext<UniverseFormData>();
  const { t } = useTranslation();
  const classes = useFormFieldStyles();

  //watchers
  const ysqlEnabled = useWatch({ name: YSQL_FIELD });
  const ycqlEnabled = useWatch({ name: YCQL_FIELD });
  const yedisEnabled = useWatch({ name: YEDIS_FIELD });
  const provider = useWatch({ name: PROVIDER_FIELD });
  const customizePort = useWatch({ name: CUSTOMIZE_PORT_FIELD });

  // Master and TSerever ports can be modified in EDIT mode when the provider is not Kubernetes.
  // Redis, YCQL, YSQL ports cannot be modified in EDIT mode but can be edited via Universe actions in Overview section.
  const portsConfig = [
    { id: 'masterHttpPort', visible: true, disabled: disabled },
    { id: 'masterRpcPort', visible: true, disabled: disabled },
    { id: 'tserverHttpPort', visible: true, disabled: disabled },
    { id: 'tserverRpcPort', visible: true, disabled: disabled },
    { id: 'redisServerHttpPort', visible: yedisEnabled, disabled: isEditMode },
    { id: 'redisServerRpcPort', visible: yedisEnabled, disabled: isEditMode },
    { id: 'yqlServerHttpPort', visible: ycqlEnabled, disabled: isEditMode },
    { id: 'yqlServerRpcPort', visible: ycqlEnabled, disabled: isEditMode },
    { id: 'ysqlServerHttpPort', visible: ysqlEnabled, disabled: isEditMode },
    { id: 'ysqlServerRpcPort', visible: ysqlEnabled, disabled: isEditMode },
    { id: 'nodeExporterPort', visible: provider?.code !== CloudType.onprem, disabled: disabled }
  ].filter((ports) => ports.visible);

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
                <Grid container>
                  {portsConfig.map((item) => (
                    <Grid lg={6} key={item.id}>
                      <Box display="flex" mr={4} mt={1}>
                        <YBLabel dataTestId={`DeploymentPortsField-${item.id}`}>
                          {t(`universeForm.advancedConfig.${item.id}`)}
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
              )}
            </Box>
          </Box>
        );
      }}
    />
  );
};

//hidden for k8s
