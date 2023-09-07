import React, { FC, FocusEvent } from 'react';
import clsx from 'clsx';
import { useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { Box, Typography } from '@material-ui/core';
import { YBCheckboxField, YBInputField } from '../../../../../components';
import {
  useEITStyles,
  EncryptionInTransitFormValues,
  ROLLING_UPGRADE_DELAY_FIELD_NAME,
  USE_ROLLING_UPGRADE_FIELD_NAME
} from '../EncryptionInTransitUtils';

export const RollingUpgrade: FC = () => {
  const { t } = useTranslation();
  const classes = useEITStyles();
  const { control, setValue } = useFormContext<EncryptionInTransitFormValues>();

  return (
    <Box mt={3}>
      <Box>
        <YBCheckboxField
          control={control}
          name={USE_ROLLING_UPGRADE_FIELD_NAME}
          label={t('universeActions.encryptionInTransit.useRollingUpgrade')}
          labelProps={{ className: classes.eitLabels }}
          inputProps={{
            'data-testid': 'UseRollingUpgrade-Checkbox'
          }}
        />
      </Box>

      <Box ml={4.5}>
        <Box mb={1.5}>
          <Typography variant="body2" className={classes.eitLabels}>
            {t('universeActions.encryptionInTransit.rollingUpgradeInputTitle')}
          </Typography>
        </Box>
        <YBInputField
          control={control}
          name={ROLLING_UPGRADE_DELAY_FIELD_NAME}
          className={clsx(classes.inputField, classes.upgradeDelayInput)}
          type="number"
          data-testid="UpgradeDelay-Input"
          inputProps={{
            min: 0
          }}
          onBlur={(e: FocusEvent<HTMLInputElement>) => {
            const inputVal = (e.target.value as unknown) as number;
            inputVal <= 0 && setValue(ROLLING_UPGRADE_DELAY_FIELD_NAME, 240);
          }}
        />
      </Box>
    </Box>
  );
};
