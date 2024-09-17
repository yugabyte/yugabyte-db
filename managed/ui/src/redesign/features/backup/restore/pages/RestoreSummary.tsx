/*
 * Created on Mon Aug 19 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import clsx from 'clsx';
import { useFormContext } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { makeStyles, Typography } from '@material-ui/core';
import { ybFormatDate } from '../../../../helpers/DateUtils';
import { formatBytes } from '../../../../../components/xcluster/ReplicationUtils';
import { Backup_Options_Type } from '../../../../../components/backupv2';
import { RestoreFormModel } from '../models/RestoreFormModel';

const useStyles = makeStyles((theme) => ({
  root: {
    padding: '24px',
    background: theme.palette.ybacolors.backgroundGrayLight,
    height: '100%',
    display: 'flex',
    flexDirection: 'column',
    gap: '32px'
  },
  card: {
    borderRadius: '8px',
    background: theme.palette.common.white,
    border: `1px solid ${theme.palette.grey[200]}`
  },
  cardHeader: {
    padding: '20px 16px',
    borderRadius: '8px 8px 0 0',
    borderBottom: `1px solid ${theme.palette.grey[200]}`
  },
  cardContent: {
    padding: '28px 16px',
    display: 'flex',
    gap: '24px',
    flexDirection: 'column',
    width: '220px'
  },
  attribute: {
    textTransform: 'uppercase',
    color: theme.palette.grey[600]
  },
  value: {
    marginTop: '10px'
  }
}));

const RestoreSummary: FC = () => {
  const classes = useStyles();

  const { t } = useTranslation('translation', { keyPrefix: 'backup.restore.backupSummary' });

  const { watch } = useFormContext<RestoreFormModel>();

  const source = watch('source');
  const target = watch('target');
  const commonBackupInfo = watch('currentCommonBackupInfo');
  const pitrMillis = watch('pitrMillis');

  const summaries = {
    [t('SOURCE')]: [
      {
        key: t('keyspaces'),
        value: source.keyspace?.label ?? '-'
      },
      {
        key: t('tables'),
        value:
          source.keyspace?.isDefaultOption || source.tableBackupType === Backup_Options_Type.ALL
            ? t('allTables')
            : t('tablesSelected', { count: source.selectedTables?.length }) + '' ?? '-'
      },
      {
        key: t('restoreTo'),
        value: pitrMillis ? ybFormatDate(pitrMillis) : '-'
      },
      {
        key: t('restoreSize'),
        value: formatBytes(commonBackupInfo?.totalBackupSizeInBytes) ?? '-'
      }
    ],
    [t('TARGET')]: [
      {
        key: t('targetUniverse'),
        value: target.targetUniverse?.label ?? '-'
      },
      {
        key: t('kmsConfig'),
        value: target.kmsConfig?.label ?? '-'
      }
    ]
  };

  const generateCard = (
    type: string,
    attributes: { key: string; value: string | JSX.Element }[]
  ) => {
    return (
      <div className={clsx(classes.card)}>
        <div className={classes.cardHeader}>
          <Typography variant="body1">{type}</Typography>
        </div>
        <div className={classes.cardContent}>
          {attributes.map((attribute, ind) => {
            return (
              <div key={ind}>
                <Typography variant="subtitle1" className={classes.attribute}>
                  {attribute.key}
                </Typography>
                <Typography variant="body2" className={classes.value}>
                  {attribute.value}
                </Typography>
              </div>
            );
          })}
        </div>
      </div>
    );
  };

  return (
    <div className={classes.root}>
      {generateCard(t('SOURCE'), summaries[t('SOURCE')])}
      {generateCard(t('TARGET'), summaries[t('TARGET')])}
    </div>
  );
};

export default RestoreSummary;
