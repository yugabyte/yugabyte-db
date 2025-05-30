import { FC } from 'react';
import _ from 'lodash';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { useFormContext, useWatch, Controller } from 'react-hook-form';
import { mui, YBToggleField, YBLabel, YBAutoComplete } from '@yugabyte-ui-library/core';
import { QUERY_KEY, api } from '../../../../features/universe/universe-form/utils/api';

import { SecuritySettingsProps } from '../../steps/security-settings/dtos';
import { KmsConfig } from '../../../../features/universe/universe-form/utils/dto';
import { ReactComponent as NextLineIcon } from '../../../../assets/next-line.svg';

const { Box } = mui;

interface EARProps {
  disabled: boolean;
}

const getOptionLabel = (op: any): string => {
  const option = (op as unknown) as KmsConfig;
  return option?.metadata?.name ?? '';
};

const EAR_FIELD = 'enableEncryptionAtRest';
const KMS_FIELD = 'kmsConfig';

export const EARField: FC<EARProps> = ({ disabled }) => {
  const { control, setValue } = useFormContext<SecuritySettingsProps>();
  const { t } = useTranslation();

  //watchers
  const encryptionEnabled = useWatch({ name: EAR_FIELD });

  //fetch data
  const { data, isLoading } = useQuery(QUERY_KEY.getKMSConfigs, api.getKMSConfigs);
  let kmsConfigs: KmsConfig[] = data ? _.sortBy(data, 'metadata.provider', 'metadata.name') : [];

  const handleChange = (e: any, option: any) => {
    setValue(KMS_FIELD, option?.metadata?.configUUID ?? null, {
      shouldValidate: true
    });
  };

  return (
    <Box
      sx={{
        display: 'flex',
        width: '548px',
        flexDirection: 'column',
        backgroundColor: '#FBFCFD',
        border: '1px solid #D7DEE4',
        borderRadius: '8px',
        padding: '16px 24px'
      }}
    >
      <YBToggleField name={EAR_FIELD} control={control} label={'Enable Encryption At Rest'} />
      {encryptionEnabled && (
        <Box
          sx={{ display: 'flex', flexDirection: 'row', width: '100%', alignItems: 'center', mt: 2 }}
        >
          <NextLineIcon />
          <Box sx={{ ml: 2, display: 'flex', width: '100%' }}>
            <Controller
              name={KMS_FIELD}
              control={control}
              rules={{
                required:
                  !disabled && encryptionEnabled
                    ? (t('universeForm.validation.required', {
                        field: t('universeForm.instanceConfig.kmsConfig')
                      }) as string)
                    : ''
              }}
              render={({ field, fieldState }) => {
                const value = kmsConfigs.find((i) => i.metadata.configUUID === field.value) ?? '';
                return (
                  <Box
                    display="flex"
                    width="100%"
                    flexDirection={'column'}
                    data-testid="KMSConfigField-Container"
                  >
                    <YBLabel error={!!fieldState.error}>Key Management Service Config</YBLabel>
                    <Box flex={1}>
                      <YBAutoComplete
                        disabled={disabled}
                        loading={isLoading}
                        options={(kmsConfigs as unknown) as Record<string, string>[]}
                        groupBy={(option: Record<string, any>) => option?.metadata?.provider} //group by provider
                        ybInputProps={{
                          placeholder: t('universeForm.instanceConfig.kmsConfigPlaceHolder'),
                          error: !!fieldState.error,
                          helperText: fieldState.error?.message,
                          InputProps: { autoFocus: true }
                        }}
                        ref={field.ref}
                        getOptionLabel={getOptionLabel}
                        onChange={handleChange}
                        value={(value as unknown) as never}
                      />
                    </Box>
                  </Box>
                );
              }}
            />
          </Box>
        </Box>
      )}
    </Box>
  );
};
