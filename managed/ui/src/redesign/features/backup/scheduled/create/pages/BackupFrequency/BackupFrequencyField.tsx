/*
 * Created on Thu Aug 08 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import { Control, useWatch } from 'react-hook-form';
import { Trans, useTranslation } from 'react-i18next';
import { makeStyles, MenuItem, Typography } from '@material-ui/core';
import {
  YBCheckboxField,
  YBInputField,
  YBSelectField,
  YBToggleField
} from '../../../../../../components';
import { BackupFrequencyModel, TimeUnit } from '../../models/IBackupFrequency';

type BackupFrequencyFieldProps = {
  control: Control<BackupFrequencyModel>;
  isEditMode?: boolean;
};

const useStyles = makeStyles((theme) => ({
  title: {
    marginBottom: '20px'
  },
  config: {
    display: 'flex',
    alignItems: 'baseline',
    gap: '8px',
    marginTop: '8px'
  },
  frequencyInput: {
    width: '120px'
  },
  cronInput: {
    width: '225px'
  },
  incBackup: {
    padding: '16px',
    borderRadius: '8px',
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    marginTop: '20px',
    width: '650px',
    display: 'flex',
    gap: '24px',
    flexDirection: 'column'
  },
  incBackupFields: {
    marginLeft: '52px',
    display: 'flex',
    gap: '6px',
    flexDirection: 'column'
  },
  incHelpText: {
    fontSize: '12px',
    fontWeight: 400,
    color: theme.palette.ybacolors.textDarkGray
  },
  frequencySelectField: {
    width: '160px'
  },
  // This is a workaround to fix the z-index issue with the select menu
  '@global': {
    'body>div#menu-frequencyTimeUnit, body>div#menu-incrementalBackupFrequencyTimeUnit': {
      zIndex: '99999 !important',
      '& .MuiListSubheader-sticky': {
        top: 'auto'
      }
    }
  }
}));

const BackupFrequencyField: FC<BackupFrequencyFieldProps> = ({ control, isEditMode = false }) => {
  const classes = useStyles();

  const { t } = useTranslation('translation', {
    keyPrefix: 'backup.scheduled.create.backupFrequency.backupFrequencyField'
  });

  const useCronExpressionVal = useWatch({
    control,
    name: 'useCronExpression'
  });

  const useIncrementalBackupVal = useWatch({
    control,
    name: 'useIncrementalBackup'
  });

  return (
    <div>
      <Typography variant="body1" className={classes.title}>
        {t('title')}
      </Typography>
      <div>{t('fullBackup')}</div>
      <div className={classes.config}>
        {!useCronExpressionVal ? (
          <>
            <div>{t('every')}</div>
            <YBInputField
              control={control}
              name={'frequency'}
              className={classes.frequencyInput}
              type="number"
              data-testid="frequency"
            />
            <YBSelectField
              control={control}
              name={'frequencyTimeUnit'}
              className={classes.frequencySelectField}
              data-testid="frequencyTimeUnit"
            >
              {Object.values(TimeUnit).map((unit) => (
                <MenuItem key={unit} value={unit}>
                  {unit}
                </MenuItem>
              ))}
            </YBSelectField>
          </>
        ) : (
          <YBInputField
            control={control}
            name="cronExpression"
            className={classes.cronInput}
            placeholder={t('cronPlaceholder')}
            data-testid="cronExpression"
          />
        )}

        <YBCheckboxField control={control} name="useCronExpression" label={t('cronExpression')} />
      </div>
      <div className={classes.incBackup}>
        <YBToggleField
          label={t('takeIncrementalBackups')}
          control={control}
          name="useIncrementalBackup"
          data-testid="useIncrementalBackup"
          disabled={isEditMode}
        />
        {useIncrementalBackupVal && (
          <div className={classes.incBackupFields}>
            <Typography variant="body2">{t('incrementalBackupTitle')}</Typography>
            <div className={classes.config}>
              <div>{t('every')}</div>
              <YBInputField
                control={control}
                name={'incrementalBackupFrequency'}
                className={classes.frequencyInput}
                type="number"
                data-testid="incrementalBackupFrequency"
              />
              <YBSelectField
                control={control}
                name={'incrementalBackupFrequencyTimeUnit'}
                className={classes.frequencySelectField}
                data-testid="incrementalBackupFrequencyTimeUnit"
              >
                {Object.values(TimeUnit).map((unit) => (
                  <MenuItem key={unit} value={unit}>
                    {unit}
                  </MenuItem>
                ))}
              </YBSelectField>
            </div>
            <div className={classes.incHelpText}>
              <Trans t={t} components={{ b: <b /> }}>
                {t('incrementalBackupHelpText')}
              </Trans>
            </div>
          </div>
        )}
      </div>
    </div>
  );
};

export default BackupFrequencyField;
