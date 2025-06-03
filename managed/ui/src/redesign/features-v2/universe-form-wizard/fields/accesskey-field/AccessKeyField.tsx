import { FC } from 'react';
import { useSelector } from 'react-redux';
import { useFormContext } from 'react-hook-form';
import { mui, YBSelectField } from '@yugabyte-ui-library/core';
import { OtherAdvancedProps, AccessKey } from '../../steps/advanced-settings/dtos';

const { Box, MenuItem } = mui;

interface AccessKeyFieldProps {
  disabled: boolean;
}

const ACCESS_KEY_FIELD = 'accessKeyCode';

export const AccessKeyField: FC<AccessKeyFieldProps> = ({ disabled }) => {
  const { setValue, control } = useFormContext<OtherAdvancedProps>();

  //all access keys
  const allAccessKeys = useSelector((state: any) => state.cloud.accessKeys).data;

  //filter access key list by provider
  //   const accessKeysList = allAccessKeys.data.filter(
  //     (item: AccessKey) => item?.idKey?.providerUUID === provider?.uuid
  //   );

  return (
    <Box sx={{ display: 'flex', flexDirection: 'column', width: '734px' }}>
      <YBSelectField
        name={ACCESS_KEY_FIELD}
        control={control}
        disabled={disabled}
        label="Access Key"
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
