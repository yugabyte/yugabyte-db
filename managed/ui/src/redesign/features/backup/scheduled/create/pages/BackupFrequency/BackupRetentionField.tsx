/*
 * Created on Mon Aug 12 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { Control, useWatch } from 'react-hook-form';
import { makeStyles, MenuItem, Typography } from '@material-ui/core';
import { BackupFrequencyModel, TimeUnit } from '../../models/IBackupFrequency';
import { YBCheckboxField, YBInputField, YBSelectField } from '../../../../../../components';

type BackupRetentionFieldProps = {
  control: Control<BackupFrequencyModel>;
};

const useStyles = makeStyles(() => ({
  root: {
    display: 'flex',
    gap: '20px',
    flexDirection: 'column'
  },
  fields: {
    display: 'flex',
    gap: '8px',
    alignItems: 'baseline'
  },
  frequencySelectField: {
    width: '160px'
  }
}));

export const BackupRetentionField: FC<BackupRetentionFieldProps> = ({ control }) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'backup.scheduled.create.backupFrequency.backupRetention'
  });

  const classes = useStyles();

  const keepIndefinitely = useWatch({
    control,
    name: 'keepIndefinitely'
  });

  return (
    <div className={classes.root}>
      <Typography variant="body1">{t('title')}</Typography>
      <div className={classes.fields}>
        <YBInputField
          control={control}
          name="expiryTime"
          type="number"
          disabled={keepIndefinitely}
          data-testid="expiryTime"
        />
        <YBSelectField
          control={control}
          name={'expiryTimeUnit'}
          className={classes.frequencySelectField}
          disabled={keepIndefinitely}
          data-testid="expiryTimeUnit"
        >
          {Object.values(TimeUnit).map((unit) => (
            <MenuItem key={unit} value={unit}>
              {unit}
            </MenuItem>
          ))}
        </YBSelectField>
        <YBCheckboxField
          control={control}
          name="keepIndefinitely"
          label={t('keepIndefinitely')}
          data-testid="keepIndefinitely"
        />
      </div>
    </div>
  );
};
