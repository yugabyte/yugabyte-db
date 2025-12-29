import { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { useSelector } from 'react-redux';
import { useFormContext } from 'react-hook-form';
import { mui, YBSelectField } from '@yugabyte-ui-library/core';
import { OtherAdvancedProps, AccessKey } from '../../steps/advanced-settings/dtos';

const { Box, MenuItem } = mui;

interface AccessKeyFieldProps {
  disabled: boolean;
  provider: string;
}

const ACCESS_KEY_FIELD = 'accessKeyCode';

export const AccessKeyField: FC<AccessKeyFieldProps> = ({ disabled, provider }) => {
  const { control } = useFormContext<OtherAdvancedProps>();
  const { t } = useTranslation();

  //all access keys
  const allAccessKeys = useSelector((state: any) => state.cloud.accessKeys).data;

  //TODO : Enable this once state is available for this new route
  // // filter access key list by provider
  // const accessKeysList = allAccessKeys?.data?.filter(
  //   (item: AccessKey) => item?.idKey?.providerUUID === (provider || '')
  // );

  // //only first time
  // useEffectOnce(() => {
  //   if (accessKeysList?.length && provider) {
  //     setValue(ACCESS_KEY_FIELD, accessKeysList[0]?.idKey.keyCode, { shouldValidate: true });
  //   } else {
  //     setValue(ACCESS_KEY_FIELD, '', { shouldValidate: true });
  //   }
  // });

  return (
    <Box sx={{ display: 'flex', flexDirection: 'column', width: '734px' }}>
      <YBSelectField
        name={ACCESS_KEY_FIELD}
        control={control}
        disabled={disabled}
        label={t('createUniverseV2.otherAdvancedSettings.accessKeyLabel')}
        dataTestId="access-key-select"
      >
        {allAccessKeys.map((item: AccessKey) => (
          <MenuItem key={item.idKey.keyCode} value={item.idKey.keyCode}>
            {item.idKey.keyCode}
          </MenuItem>
        ))}
      </YBSelectField>
    </Box>
  );
};
