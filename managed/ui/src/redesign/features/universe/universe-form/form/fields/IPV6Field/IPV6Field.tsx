import { ReactElement } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { Box } from '@material-ui/core';
import { YBLabel, YBToggleField, YBTooltip } from '../../../../../../components';
import { UniverseFormData } from '../../../utils/dto';
import { IPV6_FIELD } from '../../../utils/constants';
import InfoMessageIcon from '../../../../../../assets/info-message.svg';
import { useFormFieldStyles } from '../../../universeMainStyle';

interface IPV6FieldProps {
  disabled: boolean;
}

export const IPV6Field = ({ disabled }: IPV6FieldProps): ReactElement => {
  const { control } = useFormContext<UniverseFormData>();
  const { t } = useTranslation();
  const classes = useFormFieldStyles();
  const IPV6TooltipText = t('universeForm.advancedConfig.enableIPV6Helper');

  return (
    <Box display="flex" width="100%" data-testid="IPV6Field-Container">
      <YBLabel dataTestId="IPV6Field-Label" className={classes.advancedConfigLabel}>
        {t('universeForm.advancedConfig.enableIPV6')}
        &nbsp;
        <YBTooltip title={IPV6TooltipText}>
          <img alt="Info" src={InfoMessageIcon} />
        </YBTooltip>
      </YBLabel>
      <Box flex={1}>
        <YBToggleField
          name={IPV6_FIELD}
          inputProps={{
            'data-testid': 'IPV6Field-Toggle'
          }}
          control={control}
          disabled={disabled}
        />
      </Box>
    </Box>
  );
};

//shown only for k8s
