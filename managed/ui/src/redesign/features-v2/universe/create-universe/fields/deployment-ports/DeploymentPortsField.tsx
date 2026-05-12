/* eslint-disable react/display-name */
/*
 * Created on Mon Nov 13 2023
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormContext, Controller } from 'react-hook-form';
import { mui, YBInput } from '@yugabyte-ui-library/core';
import { CloudType } from '../../../../../helpers/dtos';
import { OtherAdvancedProps } from '../../steps/advanced-settings/dtos';
import { YSQLFormSpec, YCQLFormSpec } from '../../steps/database-settings/dtos';
import { DEFAULT_COMMUNICATION_PORTS } from '../../helpers/constants';

//icons
import NextLineIcon from '../../../../../assets/next-line.svg';
import InfoIcon from '../../../../../assets/info-new.svg';

const { Box, styled, Typography } = mui;

const MAX_PORT = 65535;
interface DeploymentPortsProps {
  disabled: boolean;
  providerCode: string;
  ysql: YSQLFormSpec;
  ycql: YCQLFormSpec;
  enableConnectionPooling?: boolean;
}

const PortContainer = styled(Box)(({ theme }) => ({
  display: 'flex',
  flexDirection: 'column',
  width: '800px',
  padding: theme.spacing(3),
  gap: theme.spacing(4),
  borderRadius: '8px',
  border: '1px solid #D7DEE4',
  backgroundColor: '#FBFCFD',
  marginBottom: '12px'
}));

const PortTitle = styled(Typography)(({ theme }) => ({
  fontSize: 13,
  lineHeight: '16px',
  fontWeight: 600,
  color: '#4E5F6D'
}));

const StyledLabelIcon = styled(Box)(({ theme }) => ({
  fontSize: '13px',
  lineHeight: '16px',
  fontWeight: 500,
  color: '#6D7C88',
  display: 'flex',
  flexDirection: 'row',
  alignItems: 'center',
  gap: '2px'
}));

export const DeploymentPortsField: FC<DeploymentPortsProps> = ({
  disabled,
  ysql,
  ycql,
  providerCode,
  enableConnectionPooling
}) => {
  const { setValue, control } = useFormContext<OtherAdvancedProps>();
  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.otherAdvancedSettings.deployPortsFeild'
  });

  const MASTER_PORTS = [
    { id: 'masterHttpPort', visible: true, disabled: disabled },
    { id: 'masterRpcPort', visible: true, disabled: disabled }
  ];

  const TSERVER_PORTS = [
    { id: 'tserverHttpPort', visible: true, disabled: disabled },
    { id: 'tserverRpcPort', visible: true, disabled: disabled }
  ];

  const YCQL_PORTS = [
    {
      id: 'yqlServerHttpPort',
      visible: ycql.enable,
      disabled: disabled
    },
    {
      id: 'yqlServerRpcPort',
      visible: ycql.enable, //ycqlEnabled,
      disabled: disabled
    }
  ].filter((ports) => ports.visible);

  const YSQL_PORTS = [
    { id: 'ysqlServerHttpPort', visible: ysql.enable, disabled: disabled }, //visible: ysqlEnabled,
    {
      id: 'ysqlServerRpcPort',
      visible: ysql.enable,
      disabled: providerCode === CloudType.kubernetes
    },
    {
      id: 'internalYsqlServerRpcPort',
      visible: ysql.enable && enableConnectionPooling,
      disabled: providerCode === CloudType.kubernetes
    }
  ].filter((ports) => ports.visible);

  const REDIS_PORTS = [
    { id: 'redisServerHttpPort', visible: false, disabled: disabled },
    { id: 'redisServerRpcPort', visible: false, disabled: disabled }
  ];

  const OTHER_PORTS = [
    { id: 'nodeExporterPort', visible: providerCode !== CloudType.onprem, disabled: disabled }, //visible: provider?.code !== CloudType.onprem,
    {
      id: 'ybControllerrRpcPort',
      visible: true,
      disabled: disabled
    }
  ];

  const PORT_GROUPS = [
    {
      name: t('masterGroup'),
      PORTS_LIST: MASTER_PORTS,
      visible: MASTER_PORTS.length > 0
    },
    {
      name: t('tServerGroup'),
      PORTS_LIST: TSERVER_PORTS,
      visible: TSERVER_PORTS.length > 0
    },
    {
      name: t('ysqlGroup'),
      PORTS_LIST: YSQL_PORTS,
      visible: YSQL_PORTS.length > 0
    },
    {
      name: t('ycqlGroup'),
      PORTS_LIST: YCQL_PORTS,
      visible: YCQL_PORTS.length > 0
    },
    {
      name: t('redisGroup'),
      PORTS_LIST: REDIS_PORTS,
      visible: REDIS_PORTS.length > 0
    },
    {
      name: t('othersGroup'),
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
                {pg.PORTS_LIST.map((item) => (
                  <Controller
                    name={item.id}
                    render={({ field: { value, onChange } }) => {
                      return (
                        <YBInput
                          value={value}
                          onChange={onChange}
                          label={
                            <StyledLabelIcon>
                              <span>{t(item.id)}</span>
                              <InfoIcon />
                            </StyledLabelIcon>
                          }
                          helperText={'Default ' + Number(DEFAULT_COMMUNICATION_PORTS[item.id])}
                          dataTestId={`deployment-ports-field-${item.id}`}
                          onBlur={(event) => {
                            let port =
                              Number(event.target.value.replace(/\D/g, '')) ||
                              Number(DEFAULT_COMMUNICATION_PORTS[item.id] as string);
                            port = port > MAX_PORT ? MAX_PORT : port;
                            onChange(port);
                          }}
                          defaultValue={DEFAULT_COMMUNICATION_PORTS[item.id]}
                          // trimWhitespace={false}
                        />
                      );
                    }}
                  />
                ))}
              </Box>
            </Box>
          </Box>
        );
      })}
    </PortContainer>
  );
};
