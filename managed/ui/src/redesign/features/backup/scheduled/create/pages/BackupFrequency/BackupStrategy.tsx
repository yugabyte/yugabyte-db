/*
 * Created on Thu Aug 08 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import clsx from 'clsx';
import { FieldValues, useController, UseControllerProps } from 'react-hook-form';
import { Trans, useTranslation } from 'react-i18next';
import { makeStyles, Typography } from '@material-ui/core';
import { YBRadio } from '../../../../../../components';
import { BackupStrategyType } from '../../models/IBackupFrequency';

type BackupStrategyProps<T extends FieldValues> = UseControllerProps<T>;

const useStyles = makeStyles((theme) => ({
  cards: {
    display: 'flex',
    gap: '16px',
    alignItems: 'center',
    marginTop: '20px'
  },
  card: {
    padding: '16px 24px',
    width: '450px',
    height: '120px',
    borderRadius: '8px',
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    background: '#FBFBFB',
    color: `#333`,
    fontSize: '13px',
    cursor: 'pointer',
    '&:hover': {
      borderColor: theme.palette.ybacolors.borderBlue
    }
  },
  active: {
    background: 'rgba(55, 113, 253, 0.05)',
    borderColor: theme.palette.ybacolors.borderBlue
  },
  title: {
    display: 'flex',
    justifyContent: 'space-between',
    alignItems: 'center',
    marginBottom: '8px'
  },
  titleText: {
    fontWeight: 700
  },
  list: {
    marginLeft: '16px',
    padding: 0,
    fontWeight: 400,
    listStyle: 'disc'
  }
}));

export const BackupStrategy = <T extends FieldValues>({
  control,
  name,
  defaultValue
}: BackupStrategyProps<T>) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'backup.scheduled.create.backupFrequency'
  });

  const classes = useStyles();

  const { field } = useController({
    control,
    name,
    defaultValue
  });

  const OptionCard = (type: BackupStrategyType) => {
    const classPrefix = type === BackupStrategyType.STANDARD ? 'standard' : 'pitr';
    return (
      <div
        className={clsx(classes.card, type === field.value && classes.active)}
        onClick={() => field.onChange(type)}
        data-testid={`backup-strategy-${type}`}
      >
        <div className={classes.title}>
          <Typography variant="body2" className={classes.titleText}>
            {t(`${classPrefix}.title`)}
          </Typography>
          <YBRadio checked={field.value === type} />
        </div>
        <ul className={classes.list}>
          <Trans
            i18nKey={`${classPrefix}.helpText`}
            components={{ li: <li />, a: <a onClick={(e) => e.stopPropagation()} href="#" /> }}
            t={t}
          />
        </ul>
      </div>
    );
  };

  return (
    <div>
      <Typography variant="body1">{t('backupStrategy')}</Typography>
      <div className={classes.cards}>
        {OptionCard(BackupStrategyType.STANDARD)}
        {OptionCard(BackupStrategyType.POINT_IN_TIME)}
      </div>
    </div>
  );
};
