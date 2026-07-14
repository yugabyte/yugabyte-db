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
import { OtherAdvancedProps } from '../../steps/advanced-settings/dtos';
import { getAccessiblePorts } from '../../utils/createUniversePayload';
import { DEFAULT_COMMUNICATION_PORTS } from '../../helpers/constants';

//icons
import NextLineIcon from '../../../../../assets/next-line.svg';
import InfoIcon from '../../../../../assets/approved/info-new.svg';

const { Box, styled, Typography } = mui;

const MAX_PORT = 65535;
interface DeploymentPortsProps {
  providerCode: string;
  ysql: boolean;
  ycql: boolean;
  enableConnectionPooling?: boolean;
  isEditMode?: boolean;
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
  ysql,
  ycql,
  providerCode,
  enableConnectionPooling,
  isEditMode
}) => {
  const { control } = useFormContext<OtherAdvancedProps>();
  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.otherAdvancedSettings.deployPortsFeild'
  });

  const PORT_GROUPS = getAccessiblePorts(
    ysql,
    ycql,
    providerCode,
    enableConnectionPooling,
    t,
    isEditMode
  );

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
                          disabled={item.disabled}
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
