import { ReactElement } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { Box } from '@material-ui/core';
import { YBLabel, YBToggleField } from '../../../../../../components';
import { UniverseFormData } from '../../../utils/dto';
import { SYSTEMD_FIELD } from '../../../utils/constants';
import { useFormFieldStyles } from '../../../universeMainStyle';

interface SystemDFieldProps {
  disabled: boolean;
}

export const SystemDField = ({ disabled }: SystemDFieldProps): ReactElement => {
  const { control } = useFormContext<UniverseFormData>();
  const { t } = useTranslation();
  const classes = useFormFieldStyles();

  return (
    <Box display="flex" width="100%" data-testid="SystemDField-Container">
      <YBToggleField
        name={SYSTEMD_FIELD}
        inputProps={{
          'data-testid': 'SystemDField-Toggle'
        }}
        control={control}
        disabled={disabled}
      />
      <Box flex={1}>
        <YBLabel dataTestId="SystemDField-Label" className={classes.advancedConfigLabel}>
          {t('universeForm.advancedConfig.enableSystemD')}
        </YBLabel>
      </Box>
    </Box>
  );
};

//shown only after provider is selected
//show for non k8s
