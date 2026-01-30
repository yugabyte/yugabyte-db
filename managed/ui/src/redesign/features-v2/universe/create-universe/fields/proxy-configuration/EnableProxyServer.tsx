import { FC } from 'react';
import { Trans, useTranslation } from 'react-i18next';
import { useFormContext, useWatch } from 'react-hook-form';
import { mui, YBToggleField, YBInputField, YBMultiEntryField } from '@yugabyte-ui-library/core';
import { StyledLink } from '@app/redesign/features-v2/universe/create-universe/components/DefaultComponents';
import { ProxyAdvancedProps } from '@app/redesign/features-v2/universe/create-universe/steps/advanced-settings/dtos';
import {
  ENABLE_PROXY_SERVER_FIELD,
  SECURE_WEB_PROXY_FIELD,
  SECURE_WEB_PROXY_SERVER_FIELD,
  SECURE_WEB_PROXY_PORT_FIELD,
  WEB_PROXY_FIELD,
  WEB_PROXY_PORT_FIELD,
  WEB_PROXY_SERVER_FIELD,
  BYPASS_PROXY_LIST_FIELD,
  BYPASS_PROXY_LIST_VALUES_FIELD
} from '@app/redesign/features-v2/universe/create-universe/fields/FieldNames';

//icons
import NextLineIcon from '@app/redesign/assets/next-line.svg';

const { Box, Typography, styled } = mui;

interface EnableProxyServerProps {
  disabled: boolean;
}

const ProxyContainer = styled(Box)(({ theme }) => ({
  display: 'flex',
  flexDirection: 'column',
  width: '734px',
  borderRadius: '8px',
  border: `1px solid ${theme.palette.grey[300]}`,
  backgroundColor: '#FBFCFD'
}));

const StyledSubText = styled(Typography)(() => ({
  color: '#4E5F6D',
  fontSize: 11.5,
  fontWeight: 400,
  lineHeight: '18px',
  marginLeft: '48px'
}));

export const EnableProxyServer: FC<EnableProxyServerProps> = ({ disabled }) => {
  const { control } = useFormContext<ProxyAdvancedProps>();

  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.proxySettings.enableProxyServer'
  });

  const enableProxyValue = useWatch({ name: ENABLE_PROXY_SERVER_FIELD });
  const secureWebProxyValue = useWatch({ name: SECURE_WEB_PROXY_FIELD });
  const webProxyValue = useWatch({ name: WEB_PROXY_FIELD });
  const byPassProxyValue = useWatch({ name: BYPASS_PROXY_LIST_FIELD });

  return (
    <ProxyContainer>
      <Box sx={{ display: 'flex', flexDirection: 'column', padding: '16px 24px' }}>
        <YBToggleField
          name={ENABLE_PROXY_SERVER_FIELD}
          control={control}
          label={t('toggleLabel')}
          dataTestId="enable-proxy-server-field"
        />
        <StyledSubText sx={{ mt: 0.5 }}>
          {t('toggleHelper')}&nbsp;
          <StyledLink>{t('learnMore')}</StyledLink>
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
                name={SECURE_WEB_PROXY_FIELD}
                control={control}
                label={t('secureWebProxyLabel')}
                dataTestId="secure-web-proxy-field"
              />
              {secureWebProxyValue && (
                <Box
                  sx={{ display: 'flex', flexDirection: 'row', alignItems: 'center', gap: '16px' }}
                >
                  <YBInputField
                    control={control}
                    name={SECURE_WEB_PROXY_SERVER_FIELD}
                    fullWidth
                    disabled={disabled}
                    label={t('serverLabel')}
                    sx={{ width: '444px' }}
                    placeholder={t('serverPlaceholder')}
                    dataTestId="secure-web-proxy-server-field"
                  />
                  <YBInputField
                    control={control}
                    name={SECURE_WEB_PROXY_PORT_FIELD}
                    fullWidth
                    disabled={disabled}
                    label={t('portLabel')}
                    sx={{ width: '96px' }}
                    placeholder={t('portPlaceholder')}
                    dataTestId="secure-web-proxy-port-field"
                  />
                </Box>
              )}
            </Box>
          </Box>
          <Box
            sx={{ display: 'flex', flexDirection: 'row', alignItems: 'flex-start', gap: '16px' }}
          >
            <NextLineIcon />
            <Box sx={{ display: 'flex', flexDirection: 'column', gap: '16px' }}>
              <YBToggleField
                name={WEB_PROXY_FIELD}
                control={control}
                label={t('webProxy')}
                dataTestId="web-proxy-field"
              />
              {webProxyValue && (
                <Box
                  sx={{ display: 'flex', flexDirection: 'row', alignItems: 'center', gap: '16px' }}
                >
                  <YBInputField
                    control={control}
                    name={WEB_PROXY_SERVER_FIELD}
                    fullWidth
                    disabled={disabled}
                    label={t('serverLabel')}
                    sx={{ width: '444px' }}
                    placeholder={t('webServerPlacehoder')}
                    dataTestId="web-proxy-server-field"
                  />
                  <YBInputField
                    control={control}
                    name={WEB_PROXY_PORT_FIELD}
                    fullWidth
                    disabled={disabled}
                    label={t('portLabel')}
                    sx={{ width: '96px' }}
                    placeholder={t('portPlaceholder')}
                    dataTestId="web-proxy-port-field"
                  />
                </Box>
              )}
            </Box>
          </Box>
          <Box
            sx={{ display: 'flex', flexDirection: 'row', alignItems: 'flex-start', gap: '16px' }}
          >
            <NextLineIcon />
            <Box sx={{ display: 'flex', flexDirection: 'column', gap: '16px' }}>
              <YBToggleField
                name={BYPASS_PROXY_LIST_FIELD}
                control={control}
                label={t('byPassProxy')}
                dataTestId="by-pass-proxy-list-field"
              />
              {byPassProxyValue && (
                <Box sx={{ gap: 'unset' }}>
                  <YBMultiEntryField
                    control={control}
                    name={BYPASS_PROXY_LIST_VALUES_FIELD}
                    disabled={disabled}
                    label={t('byPassProxy')}
                    overrideWidth={572}
                    overrideHeight={154}
                    placeholderText={t('byPassListPlaceholder')}
                    subLabel={
                      <Typography variant="body2" color="textSecondary" sx={{ fontSize: 11.5 }}>
                        <Trans
                          i18nKey="createUniverseV2.proxySettings.enableProxyServer.byPassHelper"
                          components={{
                            b: <Box component="span" sx={{ fontWeight: 600, fontSize: 11.5 }} />
                          }}
                        />
                      </Typography>
                    }
                    dataTestId="by-pass-proxy-list-values-field"
                  />
                </Box>
              )}
            </Box>
          </Box>
        </Box>
      )}
    </ProxyContainer>
  );
};
