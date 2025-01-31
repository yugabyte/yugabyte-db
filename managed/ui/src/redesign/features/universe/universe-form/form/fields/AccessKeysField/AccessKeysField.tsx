import { ReactElement } from 'react';
import { useSelector } from 'react-redux';
import { useTranslation } from 'react-i18next';
import { useFormContext, useWatch } from 'react-hook-form';
import { useEffectOnce, useUpdateEffect } from 'react-use';
import { Box, MenuItem, makeStyles } from '@material-ui/core';
import { YBLabel, YBSelectField } from '../../../../../../components';
import { AccessKey, UniverseFormData } from '../../../utils/dto';
import { ACCESS_KEY_FIELD, PROVIDER_FIELD } from '../../../utils/constants';
import { useFormFieldStyles } from '../../../universeMainStyle';

const useStyles = makeStyles((theme) => ({
  overrideMuiSelectMenu: {
    '& .MuiSelect-selectMenu': {
      display: 'block'
    }
  }
}));

interface AccessKeysFieldProps {
  disabled?: boolean;
  isEditMode?: boolean;
}

export const AccessKeysField = ({ disabled, isEditMode }: AccessKeysFieldProps): ReactElement => {
  const { control, setValue } = useFormContext<UniverseFormData>();
  const { t } = useTranslation();
  const classes = useFormFieldStyles();
  const helperClasses = useStyles();

  //watchers
  const provider = useWatch({ name: PROVIDER_FIELD });

  //all access keys
  const allAccessKeys = useSelector((state: any) => state.cloud.accessKeys);

  //filter access key list by provider
  const accessKeysList = allAccessKeys.data.filter(
    (item: AccessKey) => item?.idKey?.providerUUID === provider?.uuid
  );

  useUpdateEffect(() => {
    //get all access keys by provider
    const accessKeys = allAccessKeys.data.filter(
      (item: AccessKey) => item?.idKey?.providerUUID === provider?.uuid
    );
    if (!isEditMode) {
      if (accessKeys?.length) {
        setValue(ACCESS_KEY_FIELD, accessKeys[0]?.idKey.keyCode, { shouldValidate: true });
      } else {
        setValue(ACCESS_KEY_FIELD, null, { shouldValidate: true });
      }
    }
  }, [provider]);

  //only first time
  useEffectOnce(() => {
    if (!isEditMode) {
      if (accessKeysList?.length && provider?.uuid) {
        setValue(ACCESS_KEY_FIELD, accessKeysList[0]?.idKey.keyCode, { shouldValidate: true });
      } else {
        setValue(ACCESS_KEY_FIELD, null, { shouldValidate: true });
      }
    }
  });

  return (
    <Box display="flex" width="100%" data-testid="AccessKeysField-Container">
      <YBLabel dataTestId={'AccessKeysField-Label'} className={classes.advancedConfigLabel}>
        {t('universeForm.advancedConfig.accessKey')}
      </YBLabel>
      <Box flex={1} className={classes.defaultTextBox}>
        <YBSelectField
          className={`${classes.defaultTextBox} ${helperClasses.overrideMuiSelectMenu}`}
          rules={{
            required:
              !disabled && !provider.isOnPremManuallyProvisioned
                ? (t('universeForm.validation.required', {
                    field: t('universeForm.advancedConfig.accessKey')
                  }) as string)
                : ''
          }}
          inputProps={{
            'data-testid': 'AccessKeysField-Select'
          }}
          name={ACCESS_KEY_FIELD}
          control={control}
          disabled={disabled}
        >
          {accessKeysList.map((item: AccessKey) => (
            <MenuItem key={item.idKey.keyCode} value={item.idKey.keyCode}>
              {item.idKey.keyCode}
            </MenuItem>
          ))}
        </YBSelectField>
      </Box>
    </Box>
  );
};

//show if not k8s provider
