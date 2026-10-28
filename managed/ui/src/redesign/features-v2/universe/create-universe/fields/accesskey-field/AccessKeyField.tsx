import { FC, ReactNode } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { useQuery } from 'react-query';
import { mui, YBSelectField } from '@yugabyte-ui-library/core';
import { QUERY_KEY, api } from '@app/redesign/features/universe/universe-form/utils/api';
import { OtherAdvancedProps, AccessKey } from '../../steps/advanced-settings/dtos';
import { ACCESS_KEY_CODE_BY_PROVIDER } from '../FieldNames';
import { CloudIconBox } from '../../helpers/CloudIconBox';

const { Box, MenuItem } = mui;

interface AccessKeyFieldProps {
  disabled: boolean;
  provider: string;
  providerCode?: string;
  /** Multi-provider Node Access: logo + label (Figma). Single keeps plain label. */
  showProviderLogo?: boolean;
  showHelperText?: boolean;
}

export const AccessKeyField: FC<AccessKeyFieldProps> = ({
  disabled,
  provider,
  providerCode,
  showProviderLogo = false,
  showHelperText = true
}) => {
  const { control, setValue, getValues } = useFormContext<OtherAdvancedProps>();
  const { t } = useTranslation();
  const fieldName = `${ACCESS_KEY_CODE_BY_PROVIDER}.${provider}` as `accessKeyCodeByProvider.${string}`;

  const { data: allAccessKeys, isLoading } = useQuery(
    [QUERY_KEY.getAccessKeys, provider],
    () => api.getAccessKeys(provider),
    {
      enabled: !!provider,
      onSuccess: (data) => {
        if (!provider) return;
        const map = getValues(ACCESS_KEY_CODE_BY_PROVIDER) ?? {};
        if (map[provider]) return;
        setValue(
          ACCESS_KEY_CODE_BY_PROVIDER,
          { ...map, [provider]: data?.length ? data[0].idKey.keyCode : '' },
          { shouldValidate: true }
        );
      }
    }
  );

  const label: ReactNode = showProviderLogo ? (
    <Box sx={{ display: 'inline-flex', alignItems: 'center', gap: '8px' }}>
      <CloudIconBox code={providerCode} size={16} />
      {t('createUniverseV2.otherAdvancedSettings.accessKeyLabel')}
    </Box>
  ) : (
    t('createUniverseV2.otherAdvancedSettings.accessKeyLabel')
  );

  return (
    <Box sx={{ display: 'flex', flexDirection: 'column', width: '734px' }}>
      {!isLoading && (
        <YBSelectField
          name={fieldName}
          control={control}
          disabled={disabled}
          sx={{ width: '560px' }}
          label={label}
          dataTestId={`access-key-select-${provider}`}
          helperText={
            showHelperText
              ? t('createUniverseV2.otherAdvancedSettings.accessKeyHelper')
              : undefined
          }
        >
          {allAccessKeys?.map((item: AccessKey) => (
            <MenuItem key={item.idKey.keyCode} value={item.idKey.keyCode}>
              {item.idKey.keyCode}
            </MenuItem>
          ))}
        </YBSelectField>
      )}
    </Box>
  );
};
