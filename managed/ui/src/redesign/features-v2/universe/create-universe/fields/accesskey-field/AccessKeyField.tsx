import { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { useQuery } from 'react-query';
import { mui, YBSelectField } from '@yugabyte-ui-library/core';
import { QUERY_KEY, api } from '@app/redesign/features/universe/universe-form/utils/api';
import { OtherAdvancedProps, AccessKey } from '../../steps/advanced-settings/dtos';

const { Box, MenuItem } = mui;

interface AccessKeyFieldProps {
  disabled: boolean;
  provider: string;
}

const ACCESS_KEY_FIELD = 'accessKeyCode';

export const AccessKeyField: FC<AccessKeyFieldProps> = ({ disabled, provider }) => {
  const { control, setValue } = useFormContext<OtherAdvancedProps>();
  const { t } = useTranslation();

  //access keys
  const { data: allAccessKeys, isLoading } = useQuery(
    [QUERY_KEY.getAccessKeys, provider],
    () => api.getAccessKeys(provider),
    {
      enabled: !!provider,
      onSuccess: (data) => {
        if (data?.length && provider) {
          setValue(ACCESS_KEY_FIELD, data[0]?.idKey.keyCode);
        } else {
          setValue(ACCESS_KEY_FIELD, '');
        }
      }
    }
  );

  return (
    <Box sx={{ display: 'flex', flexDirection: 'column', width: '734px' }}>
      {!isLoading && (
        <YBSelectField
          name={ACCESS_KEY_FIELD}
          control={control}
          disabled={disabled}
          label={t('createUniverseV2.otherAdvancedSettings.accessKeyLabel')}
          dataTestId="access-key-select"
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
