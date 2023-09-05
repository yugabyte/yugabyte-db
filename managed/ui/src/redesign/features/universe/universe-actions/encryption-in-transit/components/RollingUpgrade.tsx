import React, { FC } from 'react';
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
  const { control } = useFormContext<EncryptionInTransitFormValues>();

  return (
    <Box mt={3}>
      <Box>
        <YBCheckboxField
          control={control}
          name={USE_ROLLING_UPGRADE_FIELD_NAME}
          label={t('universeActions.encryptionInTransit.useRollingUpgrade')}
          labelProps={{ className: classes.eitLabels }}
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
        />
      </Box>
    </Box>
  );
};
