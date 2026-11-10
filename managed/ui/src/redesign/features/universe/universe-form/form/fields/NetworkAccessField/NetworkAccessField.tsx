import { ReactElement } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormContext, useWatch } from 'react-hook-form';
import { Box } from '@material-ui/core';
import { YBLabel, YBToggle, YBTooltip } from '../../../../../../components';
import { UniverseFormData, ExposingServiceTypes } from '../../../utils/dto';
import { EXPOSING_SERVICE_FIELD } from '../../../utils/constants';
import InfoMessageIcon from '../../../../../../assets/info-message.svg';
import { useFormFieldStyles } from '../../../universeMainStyle';

interface NetworkAccessFieldProps {
  disabled: boolean;
}

export const NetworkAccessField = ({ disabled }: NetworkAccessFieldProps): ReactElement => {
  const { setValue } = useFormContext<UniverseFormData>();
  const { t } = useTranslation();
  const classes = useFormFieldStyles();
  // Tooltip text
  const networkAccessTooltipText = t('universeForm.advancedConfig.enableNetworkAccessHelper');
  //watchers
  const exposingServiceValue = useWatch({ name: EXPOSING_SERVICE_FIELD });

  const handleChange = (event: any) => {
    setValue(
      EXPOSING_SERVICE_FIELD,
      event.target.checked ? ExposingServiceTypes.EXPOSED : ExposingServiceTypes.UNEXPOSED
    );
  };

  return (
    <Box display="flex" width="100%" data-testid="NetworkAccessField-Container">
      <YBLabel dataTestId="NetworkAccessField-Label" className={classes.advancedConfigLabel}>
        {t('universeForm.advancedConfig.enableNetworkAccess')}
        &nbsp;
        <YBTooltip title={networkAccessTooltipText}>
          <img alt="Info" src={InfoMessageIcon} />
        </YBTooltip>
      </YBLabel>
      <Box flex={1}>
        <YBToggle
          inputProps={{
            'data-testid': 'NetworkAccessField-Toggle'
          }}
          disabled={disabled}
          onChange={handleChange}
          checked={exposingServiceValue === ExposingServiceTypes.EXPOSED}
        />
      </Box>
    </Box>
  );
};

//shown only for k8s
