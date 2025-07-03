/* eslint-disable react/display-name */
/*
 * Created on Mon Nov 13 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import { useFormContext, FieldPath } from 'react-hook-form';
import { mui, YBInputField } from '@yugabyte-ui-library/core';
import { OtherAdvancedProps } from '../../steps/advanced-settings/dtos';
import { DEFAULT_COMMUNICATION_PORTS } from '../../helpers/constants';

const { Box, styled, Typography } = mui;

import { ReactComponent as NextLineIcon } from '../../../../assets/next-line.svg';

interface DeploymentPortsProps {
  disabled: boolean;
}

const PortContainer = styled(Box)(({ theme }) => ({
  display: 'flex',
  flexDirection: 'column',
  width: '800px',
  padding: theme.spacing(3),
  gap: theme.spacing(4),
  borderRadius: '8px',
  border: '1px solid #D7DEE4',
  backgroundColor: '#FBFCFD'
}));

const PortTitle = styled(Typography)(({ theme }) => ({
  fontSize: 13,
  lineHeight: '16px',
  fontWeight: 600,
  color: '#4E5F6D'
}));

export const DeploymentPortsField: FC<DeploymentPortsProps> = ({ disabled }) => {
  const { setValue, control } = useFormContext<OtherAdvancedProps>();

  const MASTER_PORTS = [
    { id: 'masterHttpPort', visible: true, disabled: disabled, label: 'Master HTTP Port' },
    { id: 'masterRpcPort', visible: true, disabled: disabled, label: 'Master RPC Port' }
  ];

  const TSERVER_PORTS = [
    { id: 'tserverHttpPort', visible: true, disabled: disabled, label: 'T-Server HTTP Port' },
    { id: 'tserverRpcPort', visible: true, disabled: disabled, label: 'T-Server RPC Port' }
  ];

  const YCQL_PORTS = [
    {
      id: 'yqlServerHttpPort',
      visible: true, //ycqlEnabled,
      disabled: disabled,
      label: 'YCQL HTTP Port'
    },
    {
      id: 'yqlServerRpcPort',
      visible: true, //ycqlEnabled,
      disabled: disabled,
      label: 'YCQL Port'
    }
  ].filter((ports) => ports.visible);

  const YSQL_PORTS = [
    { id: 'ysqlServerHttpPort', visible: true, disabled: disabled, label: 'YSQL HTTP Port' }, //visible: ysqlEnabled,
    {
      id: 'ysqlServerRpcPort',
      visible: true,
      // visible: ysqlEnabled,
      disabled: disabled,
      // disabled: isEditMode || provider?.code == CloudType.kubernetes,
      // tooltip: (
      //   <Trans
      //     i18nKey={'universeForm.advancedConfig.dbRPCPortTooltip'}
      //     values={{ port: DEFAULT_COMMUNICATION_PORTS.ysqlServerRpcPort }}
      //   />
      // ),
      label: 'YSQL Port'
    },
    {
      id: 'internalYsqlServerRpcPort',
      // visible: ysqlEnabled && connectionPoolingEnabled,
      // disabled: isEditMode || provider?.code == CloudType.kubernetes,
      visible: true,
      disabled: disabled,
      label: 'Internal YSQL Port'
    }
  ].filter((ports) => ports.visible);

  const REDIS_PORTS = [
    { id: 'redisServerHttpPort', visible: false, disabled: disabled, label: 'Redis HTTP Port' },
    { id: 'redisServerRpcPort', visible: false, disabled: disabled, label: 'Redis HTTP Port' }
  ];

  const OTHER_PORTS = [
    { id: 'nodeExporterPort', visible: true, disabled: disabled, label: 'Node Exporter Port' }, //visible: provider?.code !== CloudType.onprem,
    {
      id: 'ybControllerrRpcPort',
      visible: true,
      disabled: disabled,
      label: 'YB Controller RPC Port'
    }
  ];

  const PORT_GROUPS = [
    {
      name: 'Master Server',
      PORTS_LIST: MASTER_PORTS,
      visible: MASTER_PORTS.length > 0
    },
    {
      name: 'T-Server',
      PORTS_LIST: TSERVER_PORTS,
      visible: TSERVER_PORTS.length > 0
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
      name: 'Redis',
      PORTS_LIST: REDIS_PORTS,
      visible: REDIS_PORTS.length > 0
    },
    {
      name: 'Others',
      PORTS_LIST: OTHER_PORTS,
      visible: OTHER_PORTS.length > 0
    }
  ].filter((pg) => pg.visible);

  return (
    <PortContainer>
      {PORT_GROUPS.map((pg) => {
        return (
          <Box sx={{ display: 'flex', flexDirection: 'column', gap: '16px' }} key={pg.name}>
            <PortTitle>{pg.name}</PortTitle>
            <Box sx={{ display: 'flex', flexDirection: 'row', alignItems: 'center', gap: '24px' }}>
              <NextLineIcon />
              <Box
                sx={{ display: 'flex', flexDirection: 'row', alignItems: 'center', gap: '16px' }}
              >
                {pg.PORTS_LIST.map((port) => {
                  return (
                    <YBInputField
                      name={port.id as FieldPath<OtherAdvancedProps>}
                      control={control}
                      label={port.label}
                      defaultValue={Number(DEFAULT_COMMUNICATION_PORTS[port.id])}
                      helperText={'Default ' + Number(DEFAULT_COMMUNICATION_PORTS[port.id])}
                      sx={{ width: '180px' }}
                    />
                  );
                })}
              </Box>
            </Box>
          </Box>
        );
      })}
    </PortContainer>
  );
};
