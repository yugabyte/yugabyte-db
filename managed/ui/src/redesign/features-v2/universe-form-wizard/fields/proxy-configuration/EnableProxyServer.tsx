import { FC } from 'react';
import { useFormContext, useWatch } from 'react-hook-form';
import { mui, YBToggleField, YBInputField } from '@yugabyte-ui-library/core';
import { StyledLink } from '../../components/DefaultComponents';
import { ProxyAdvancedProps } from '../../steps/advanced-settings/dtos';

const { Box, Typography, styled } = mui;

import { ReactComponent as NextLineIcon } from '../../../../assets/next-line.svg';

interface EnableProxyServerProps {
  disabled: boolean;
}

const ProxyContainer = styled(Box)(({ theme }) => ({
  display: 'flex',
  flexDirection: 'column',
  width: '734px',
  borderRadius: '8px',
  border: '1px solid #D7DEE4',
  backgroundColor: '#FBFCFD'
}));

const StyledSubText = styled(Typography)(({ theme }) => ({
  color: '#4E5F6D',
  fontSize: 11.5,
  fontWeight: 400,
  lineHeight: '18px',
  marginLeft: '40px'
}));

export const EnableProxyServer: FC<EnableProxyServerProps> = ({ disabled }) => {
  const { control, setValue } = useFormContext<ProxyAdvancedProps>();

  const enableProxyValue = useWatch({ name: 'enableProxyServer' });
  const secureWebProxyValue = useWatch({ name: 'secureWebProxy' });
  const byPassProxyValue = useWatch({ name: 'byPassProxyList' });

  return (
    <ProxyContainer>
      <Box sx={{ display: 'flex', flexDirection: 'column', padding: '16px 24px' }}>
        <YBToggleField name={'enableProxyServer'} control={control} label={'Enable Proxy Server'} />
        <StyledSubText>
          Configure web proxies as gateways for traffic from your database nodes.
          <StyledLink>Learn more</StyledLink>
        </StyledSubText>
      </Box>
      {enableProxyValue && (
        <Box
          sx={{
            display: 'flex',
            flexDirection: 'column',
            borderTop: '1px solid #D7DEE4',
            padding: '16px 24px',
            gap: '32px'
          }}
        >
          <Box
            sx={{ display: 'flex', flexDirection: 'row', alignItems: 'flex-start', gap: '16px' }}
          >
            <NextLineIcon />
            <Box sx={{ display: 'flex', flexDirection: 'column', gap: '16px' }}>
              <YBToggleField
                name={'secureWebProxy'}
                control={control}
                label={'Secure Web Proxy (HTTPS)'}
              />
              {secureWebProxyValue && (
                <Box
                  sx={{ display: 'flex', flexDirection: 'row', alignItems: 'center', gap: '16px' }}
                >
                  <YBInputField
                    control={control}
                    name={'secureWebProxyServer'}
                    fullWidth
                    disabled={disabled}
                    label={'Server'}
                    sx={{ width: '444px' }}
                    placeholder={'https://proxy.example.com'}
                  />
                  <YBInputField
                    control={control}
                    name={'secureWebProxyPort'}
                    fullWidth
                    disabled={disabled}
                    label={'Port'}
                    sx={{ width: '96px' }}
                    placeholder={'8080'}
                  />
                </Box>
              )}
            </Box>
          </Box>
          <Box sx={{ display: 'flex', flexDirection: 'row', alignItems: 'center', gap: '16px' }}>
            <NextLineIcon />
            <YBToggleField name={'webProxy'} control={control} label={'Web Proxy (HTTP)'} />
          </Box>
          <Box
            sx={{ display: 'flex', flexDirection: 'row', alignItems: 'flex-start', gap: '16px' }}
          >
            <NextLineIcon />
            <Box sx={{ display: 'flex', flexDirection: 'column', gap: '16px' }}>
              <YBToggleField
                name={'byPassProxyList'}
                control={control}
                label={'Bypass Proxy List'}
              />
              {byPassProxyValue && (
                <YBInputField
                  control={control}
                  name={'byPassProxyListValues'}
                  fullWidth
                  disabled={disabled}
                  label={'Bypass Proxy List (WIP)'}
                  sx={{ width: '572px' }}
                  multiline={true}
                  rows={10}
                  placeholder={'example.com, example.com:8080'}
                  helperText="Separate multiple values by pressing Enter, Space, or Comma."
                />
              )}
            </Box>
          </Box>
        </Box>
      )}
    </ProxyContainer>
  );
};
