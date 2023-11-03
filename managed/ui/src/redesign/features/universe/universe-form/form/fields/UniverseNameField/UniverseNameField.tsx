import { ReactElement } from 'react';
import _ from 'lodash';
import { useTranslation } from 'react-i18next';
import { useFormContext, ValidateResult, Controller, useWatch } from 'react-hook-form';
import { Box } from '@material-ui/core';
import { YBLabel, YBInput } from '../../../../../../components';
import { api } from '../../../../../../helpers/api';
import { UniverseFormData, CloudType } from '../../../utils/dto';
import { UNIVERSE_NAME_FIELD, PROVIDER_FIELD } from '../../../utils/constants';
import { useFormFieldStyles } from '../../../universeMainStyle';

interface UniverseNameFieldProps {
  disabled?: boolean;
}

export const UniverseNameField = ({ disabled }: UniverseNameFieldProps): ReactElement => {
  const { control } = useFormContext<UniverseFormData>();
  const classes = useFormFieldStyles();
  const { t } = useTranslation();

  const provider = useWatch({ name: PROVIDER_FIELD });

  // TODO : Move this to global helper file
  const trimSpecialChars = (string: String) => {
    return string?.replace(/[^a-zA-Z0-9/-]+/g, '');
  };

  //validate universe name if exists
  const validateUniverseName = async (value_: unknown): Promise<ValidateResult> => {
    const value = value_ as string;
    if (disabled || !value_) return true; // don't validate disabled field or empty
    if ([CloudType.gcp, CloudType.kubernetes].includes(provider?.code)) {
      const specialCharsRegex = /^[a-z0-9-]*$/;
      const displayCode =
        provider?.code === CloudType.gcp
          ? CloudType.gcp.toUpperCase()
          : _.upperFirst(CloudType.kubernetes);
      if (!specialCharsRegex.test(value))
        return t('universeForm.validation.universeNameValidation', {
          cloudType: displayCode
        }) as string;
    }
    try {
      const universeList = await api.findUniverseByName(value);
      return universeList?.length
        ? (t('universeForm.cloudConfig.universeNameInUse') as string)
        : true;
    } catch (error) {
      // skip exceptions happened due to canceling previous request
      return !api.isRequestCancelError(error) ? true : (t('common.genericFailure') as string);
    }
  };

  return (
    <Box display="flex" width="100%" data-testid="UniverseNameField-Container">
      <YBLabel dataTestId="UniverseNameField-Label">
        {t('universeForm.cloudConfig.universeName')}
      </YBLabel>
      <Box flex={1} className={classes.defaultTextBox}>
        <Controller
          control={control}
          name={UNIVERSE_NAME_FIELD}
          rules={{
            validate: validateUniverseName,
            required: !disabled
              ? (t('universeForm.validation.required', {
                  field: t('universeForm.cloudConfig.universeName')
                }) as string)
              : ''
          }}
          render={({ field: { onChange, ref, ...rest }, fieldState }) => (
            <YBInput
              fullWidth
              inputRef={ref}
              disabled={disabled}
              onChange={(e) => {
                onChange(trimSpecialChars(e.target.value));
              }}
              {...rest}
              inputProps={{
                'data-testid': 'UniverseNameField-Input'
              }}
              error={!!fieldState.error}
              placeholder={t('universeForm.cloudConfig.placeholder.enterUniverseName')}
              helperText={fieldState.error?.message ?? ''}
            />
          )}
        />
      </Box>
    </Box>
  );
};
