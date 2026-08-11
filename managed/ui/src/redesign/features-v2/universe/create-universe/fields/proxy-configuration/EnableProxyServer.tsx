import { FC, FormEvent, KeyboardEvent, WheelEvent } from 'react';
import { Trans, useTranslation } from 'react-i18next';
import {
  Controller,
  FieldError,
  Merge,
  useFormContext,
  useWatch,
  UseFormSetValue
} from 'react-hook-form';
import {
  mui,
  YBToggleField,
  YBInputField,
  YBMultiEntry,
  YBCheckboxField,
  YBTooltip,
  YBHelper,
  YBHelperVariants
} from '@yugabyte-ui-library/core';
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

import InfoIcon from '../../../../../assets/info-new.svg';
import WarningIcon from '../../../../../assets/warning-triangle.svg';

const { Box, Typography, styled } = mui;

type BypassListFieldError =
  | FieldError
  | (FieldError | undefined)[]
  | Merge<FieldError, (FieldError | undefined)[]>
  | undefined;

function bypassListFieldHasError(fieldErr: BypassListFieldError): boolean {
  if (!fieldErr) return false;
  if (Array.isArray(fieldErr)) return fieldErr.some(Boolean);
  if (fieldErr.message) return true;
  const rec = fieldErr as Record<string, unknown>;
  return Object.keys(rec).some((k) => /^\d+$/.test(k) && rec[k]);
}

const bypassErrorsForMultiEntry = (
  fieldErr: BypassListFieldError,
  valuesLength: number
): FieldError[] | boolean | undefined => {
  if (!fieldErr) return undefined;
  if (Array.isArray(fieldErr)) {
    return fieldErr.some(Boolean) ? (fieldErr as FieldError[]) : undefined;
  }
  const obj = fieldErr as Record<string | number, unknown> & FieldError;
  const numericKeys = Object.keys(obj).filter((k) => /^\d+$/.test(k));
  const maxFromKeys = numericKeys.length ? Math.max(...numericKeys.map(Number)) : -1;
  const len = Math.max(
    valuesLength,
    maxFromKeys + 1,
    obj.message && numericKeys.length === 0 ? 1 : 0
  );
  if (len <= 0) return undefined;
  const out: FieldError[] = [];
  for (let i = 0; i < len; i++) {
    const e = obj[i] as FieldError | undefined;
    if (e?.message) {
      out[i] = e;
    }
  }
  if (obj.message && numericKeys.length === 0 && len >= 1 && !out[0]) {
    out[0] = {
      type: obj.type ?? 'validation',
      message: obj.message
    };
  }
  return out.some((x) => x?.message) ? out : undefined;
};

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

const SectionCard = styled(Box)(({ theme }) => ({
  display: 'flex',
  flexDirection: 'column',
  borderRadius: '8px',
  width: '686px',
  border: `1px solid ${theme.palette.grey[300]}`,
  backgroundColor: '#F7F9FB',
  padding: '16px',
  gap: '16px'
}));

const StyledSubText = styled(Typography)(() => ({
  color: '#4E5F6D',
  fontSize: 11.5,
  fontWeight: 400,
  lineHeight: '18px'
}));

function proxyPortInputProps(
  field: typeof SECURE_WEB_PROXY_PORT_FIELD | typeof WEB_PROXY_PORT_FIELD,
  setValue: UseFormSetValue<ProxyAdvancedProps>
) {
  const sync = (el: HTMLInputElement) => {
    el.value = el.value.replace(/\D/g, '');
    const n = parseInt(el.value, 10);
    const v = el.value === '' || n === 0 ? undefined : n;
    if (v === undefined) el.value = '';
    setValue(field, v, { shouldValidate: true, shouldDirty: true });
  };
  return {
    min: 1,
    max: 65535,
    inputMode: 'numeric' as const,
    onWheel: (e: WheelEvent<HTMLInputElement>) => e.currentTarget.blur(),
    onKeyDown: (e: KeyboardEvent<HTMLInputElement>) => {
      if (['e', 'E', '+', '-', '.', ','].includes(e.key)) e.preventDefault();
    },
    onInput: (e: FormEvent<HTMLInputElement>) => sync(e.currentTarget)
  };
}

export const EnableProxyServer: FC<EnableProxyServerProps> = ({ disabled }) => {
  const {
    control,
    setValue,
    formState: { errors }
  } = useFormContext<ProxyAdvancedProps>();

  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.proxySettings.enableProxyServer'
  });
  const { t: tProxy } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.proxySettings'
  });

  const enableProxyValue = useWatch({ name: ENABLE_PROXY_SERVER_FIELD });
  const secureWebProxyValue = useWatch({ name: SECURE_WEB_PROXY_FIELD });
  const webProxyValue = useWatch({ name: WEB_PROXY_FIELD });
  const byPassProxyValue = useWatch({ name: BYPASS_PROXY_LIST_FIELD });
  const bypassListValues = useWatch({ name: BYPASS_PROXY_LIST_VALUES_FIELD });

  const bypassListFieldErr = errors.byPassProxyListValues;
  const hasBypassListFieldError = bypassListFieldHasError(bypassListFieldErr);
  const hasNonEmptyBypassEntry = (bypassListValues ?? []).some(
    (v: string) => String(v ?? '').trim().length > 0
  );
  const bypassListHelperText = hasBypassListFieldError
    ? hasNonEmptyBypassEntry
      ? tProxy('validation.bypassInvalidEntries')
      : tProxy('validation.bypassRequired')
    : undefined;
  const bypassEntryErrors = bypassErrorsForMultiEntry(
    bypassListFieldErr,
    (bypassListValues ?? []).length
  );

  return (
    <ProxyContainer>
      <Box sx={{ display: 'flex', flexDirection: 'column', padding: '16px 24px' }}>
        <Box height={46}>
          <YBToggleField
            name={ENABLE_PROXY_SERVER_FIELD}
            control={control}
            label={t('toggleLabel')}
            dataTestId="enable-proxy-server-field"
          />
          <StyledSubText sx={{ marginLeft: '48px' }}>
            {t('toggleHelper')}&nbsp;
            <StyledLink>{t('learnMore')}</StyledLink>
          </StyledSubText>
        </Box>
      </Box>
      {enableProxyValue && (
        <Box
          sx={{
            display: 'flex',
            flexDirection: 'column',
            gap: '24px',
            px: '24px',
            pb: '24px',
            pt: 0
          }}
        >
          <SectionCard>
            <Box>
              <Typography variant="body2">{tProxy('proxyServerType.heading')}</Typography>

              {errors.proxyServerType?.message ? (
                <Box sx={{ mt: 1, alignItems: 'center' }}>
                  <YBHelper variant={YBHelperVariants.ERROR} icon={<WarningIcon />}>
                    {errors.proxyServerType.message}
                  </YBHelper>
                </Box>
              ) : (
                <StyledSubText sx={{ mt: 1 }}>{tProxy('proxyServerType.subheading')}</StyledSubText>
              )}
            </Box>
            <Box sx={{ display: 'flex', flexDirection: 'column', gap: '24px', ml: 5 }}>
              <Box sx={{ display: 'flex', flexDirection: 'column', gap: '16px' }}>
                <Box height={24}>
                  <YBCheckboxField
                    size="large"
                    name={SECURE_WEB_PROXY_FIELD}
                    control={control}
                    label={t('secureWebProxyLabel')}
                    disabled={disabled}
                    labelGap={1}
                    dataTestId="secure-web-proxy-field"
                  />
                </Box>
                {secureWebProxyValue && (
                  <Box
                    sx={{
                      display: 'flex',
                      flexDirection: 'row',
                      alignItems: 'flex-start',
                      gap: '16px',
                      ml: 4
                    }}
                  >
                    <Box sx={{ flex: '1 1 0', minWidth: 0, maxWidth: '444px' }}>
                      <YBInputField
                        control={control}
                        name={SECURE_WEB_PROXY_SERVER_FIELD}
                        fullWidth
                        disabled={disabled}
                        label={t('secureWebProxyEndpoint')}
                        placeholder={t('serverPlaceholder')}
                        dataTestId="secure-web-proxy-server-field"
                      />
                    </Box>
                    <Box sx={{ flex: '0 0 auto', width: '96px' }}>
                      <YBInputField
                        control={control}
                        name={SECURE_WEB_PROXY_PORT_FIELD}
                        fullWidth
                        disabled={disabled}
                        label={t('portLabel')}
                        type="number"
                        slotProps={{
                          input: proxyPortInputProps(SECURE_WEB_PROXY_PORT_FIELD, setValue)
                        }}
                        placeholder={t('portPlaceholder')}
                        dataTestId="secure-web-proxy-port-field"
                      />
                    </Box>
                  </Box>
                )}
              </Box>
              <Box sx={{ display: 'flex', flexDirection: 'column', gap: '16px' }}>
                <Box height={24}>
                  <YBCheckboxField
                    size="large"
                    name={WEB_PROXY_FIELD}
                    control={control}
                    label={t('webProxy')}
                    disabled={disabled}
                    labelGap={1}
                    dataTestId="web-proxy-field"
                  />
                </Box>
                {webProxyValue && (
                  <Box
                    sx={{
                      display: 'flex',
                      flexDirection: 'row',
                      alignItems: 'flex-start',
                      gap: '16px',
                      ml: 4
                    }}
                  >
                    <Box sx={{ flex: '1 1 0', minWidth: 0, maxWidth: '444px' }}>
                      <YBInputField
                        control={control}
                        name={WEB_PROXY_SERVER_FIELD}
                        fullWidth
                        disabled={disabled}
                        label={t('webProxyLabel')}
                        placeholder={t('webServerPlacehoder')}
                        dataTestId="web-proxy-server-field"
                      />
                    </Box>
                    <Box sx={{ flex: '0 0 auto', width: '96px' }}>
                      <YBInputField
                        control={control}
                        name={WEB_PROXY_PORT_FIELD}
                        fullWidth
                        disabled={disabled}
                        label={t('portLabel')}
                        type="number"
                        slotProps={{ input: proxyPortInputProps(WEB_PROXY_PORT_FIELD, setValue) }}
                        placeholder={t('portPlaceholder')}
                        dataTestId="web-proxy-port-field"
                      />
                    </Box>
                  </Box>
                )}
              </Box>
            </Box>
          </SectionCard>

          <SectionCard>
            <Box
              sx={{
                display: 'flex',
                alignItems: 'flex-start',
                gap: 1,
                height: 24,
                boxSizing: 'border-box'
              }}
            >
              <YBToggleField
                name={BYPASS_PROXY_LIST_FIELD}
                control={control}
                label={tProxy('bypassProxySection.toggleLabel')}
                dataTestId="by-pass-proxy-list-field"
              />
              <YBTooltip title={tProxy('bypassProxySection.tooltip')}>
                <Box
                  component="span"
                  sx={{ cursor: 'pointer', display: 'inline-flex', alignItems: 'center' }}
                  height={24}
                >
                  <InfoIcon />
                </Box>
              </YBTooltip>
            </Box>
            {byPassProxyValue && (
              <Box sx={{ gap: 'unset', ml: 5 }}>
                <Controller
                  name={BYPASS_PROXY_LIST_VALUES_FIELD}
                  control={control}
                  render={({ field }) => (
                    <YBMultiEntry
                      label={t('byPassProxy')}
                      onChange={field.onChange}
                      value={field.value}
                      error={bypassEntryErrors ?? hasBypassListFieldError}
                      helperText={hasBypassListFieldError ? bypassListHelperText : undefined}
                      disabled={disabled}
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
                  )}
                />
              </Box>
            )}
          </SectionCard>
        </Box>
      )}
    </ProxyContainer>
  );
};
