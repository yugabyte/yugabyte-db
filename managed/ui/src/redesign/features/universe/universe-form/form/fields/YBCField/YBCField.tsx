import { ReactElement } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { Box } from '@material-ui/core';
import { YBInputField, YBLabel } from '../../../../../../components';
import { UniverseFormData } from '../../../utils/dto';
import { YBC_PACKAGE_PATH_FIELD } from '../../../utils/constants';

interface YBCFieldProps {
  disabled?: boolean;
}

export const YBCField = ({ disabled }: YBCFieldProps): ReactElement => {
  const { control } = useFormContext<UniverseFormData>();
  const { t } = useTranslation();

  return (
    <Box display="flex" width="100%" data-testid="YBCField-Container">
      <YBLabel dataTestId="YBCField-Label">
        {t('universeForm.advancedConfig.ybcPackagePath')}
      </YBLabel>
      <Box flex={1}>
        <YBInputField
          control={control}
          name={YBC_PACKAGE_PATH_FIELD}
          fullWidth
          disabled={disabled}
          inputProps={{
            'data-testid': 'YBCField-Input'
          }}
        />
      </Box>
    </Box>
  );
};

//show if enableYbc feature flag enabled
