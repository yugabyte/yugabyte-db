/*
 * Created on Mon Aug 12 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { forwardRef, Fragment, useContext, useImperativeHandle } from 'react';
import cronstrue from 'cronstrue';
import { useMutation } from 'react-query';
import { toast } from 'react-toastify';
import { values } from 'lodash';
import { Trans, useTranslation } from 'react-i18next';

import { Divider, makeStyles, Typography } from '@material-ui/core';
import { AlertVariant, YBAlert } from '../../../../../../components';
import { YBTag, YBTag_Types } from '../../../../../../../components/common/YBTag';
import {
  ExtendedBackupScheduleProps,
  Page,
  PageRef,
  ScheduledBackupContext,
  ScheduledBackupContextMethods
} from '../../models/ScheduledBackupContext';
import { BACKUP_API_TYPES } from '../../../../../../../components/backupv2';

import { GetUniverseUUID, prepareScheduledBackupPayload } from '../../../ScheduledBackupUtils';

import { createScheduledBackup } from '../../../api/api';

import { ReactComponent as CheckIcon } from '../../../../../../assets/check-white.svg';
import BulbIcon from '../../../../../../assets/bulb.svg';

const useStyles = makeStyles((theme) => ({
  root: {
    padding: '24px',
    display: 'flex',
    flexDirection: 'column',
    gap: '24px'
  },
  summary: {
    padding: '8px 16px',
    borderRadius: '8px',
    border: `1px solid var(--boarder-gray, ${theme.palette.ybacolors.ybBorderGray})`,
    background: `#FBFBFB`
  },
  table: {
    width: '100%',
    '& td': {
      height: '32px',
      paddingLeft: '16px'
    },
    '& tr>td:nth-child(1):not(.MuiDivider-root)': {
      color: theme.palette.ybacolors.textDarkGray,
      textTransform: 'uppercase',
      width: '180px',
      fontSize: '11.5px'
    },
    '& .yb-tag.yb-gray:nth-child(1)': {
      marginLeft: 0
    }
  },
  divider: {
    margin: '14px 0'
  },
  helpText: {
    display: 'flex',
    borderRadius: '8px',
    border: '1px solid #E5E5E9',
    padding: '12px 16px',
    gap: '10px',
    '& > img': {
      width: '18px',
      height: '18px'
    },
    '& a': {
      textDecoration: 'underline',
      color: theme.palette.ybacolors.textDarkGray,
      cursor: 'pointer'
    }
  },
  successMsg: {
    display: 'flex',
    gap: '8px',
    alignItems: 'center'
  }
}));

const BackupSummary = forwardRef<PageRef>((_, forwardRef) => {
  
  const [scheduledBackupContext, { setPage, setIsSubmitting }, { hideModal }] = (useContext(
    ScheduledBackupContext
  ) as unknown) as ScheduledBackupContextMethods;

  const {
    formData: { generalSettings, backupFrequency, backupObjects }
  } = scheduledBackupContext;

  const classes = useStyles();
  const universeUUID = GetUniverseUUID();

  const doCreateScheduledBackup = useMutation(
    (payload: ExtendedBackupScheduleProps) => createScheduledBackup(payload),
    {
      onSuccess: () => {
        toast.success(
          <span className={classes.successMsg} style={{ display: 'flex' }}>
            <CheckIcon />
            {t('successMsg')}
          </span>
        );
        hideModal();
      },
      onError: (error: any) => {
        toast.error(error?.response?.data?.error || t('errorMsg'));
      }
    }
  );

  const { t } = useTranslation('translation', {
    keyPrefix: 'backup.scheduled.create.backupSummary'
  });

  useImperativeHandle(
    forwardRef,
    () => ({
      onNext: async () => {
        try {
          setIsSubmitting(true);
          const payload = prepareScheduledBackupPayload(scheduledBackupContext, universeUUID!);
          await doCreateScheduledBackup.mutateAsync(payload as any);
        } finally {
          setIsSubmitting(false);
        }
      },
      onPrev: () => {
        setPage(Page.BACKUP_FREQUENCY);
      }
    }),
    []
  );

  const apiType = backupObjects.keyspace?.tableType === BACKUP_API_TYPES.YSQL ? 'Ysql' : 'Ycql';
  const dbType = apiType === 'Ysql' ? 'database' : 'keyspace';

  const fullBackup = backupFrequency.useCronExpression
    ? cronstrue.toString(backupFrequency.cronExpression)
    : `${t('every', {
      keyPrefix: 'backup.scheduled.create.backupFrequency.backupFrequencyField'
    })} ${backupFrequency.frequency} ${backupFrequency.frequencyTimeUnit}`;

  let tablesList: JSX.Element | string = t('allTables');

  if (backupObjects.selectedTables?.length) {
    const allTables = [...backupObjects.selectedTables];
    const maxTablesToDisplay = allTables.splice(0, 5);
    tablesList = (
      <span>
        {maxTablesToDisplay.map((table) => (
          <YBTag type={YBTag_Types.YB_GRAY}>{table.tableName}</YBTag>
        ))}
        {allTables.length > 1 && (
          <YBTag type={YBTag_Types.PRIMARY}>
            {t('table.moreRoles', {
              count: allTables.length,
              keyPrefix: 'rbac.users.list'
            })}
          </YBTag>
        )}
      </span>
    );
  }

  const summaries = {
    generalSettings: [
      {
        name: t('name'),
        value: generalSettings.scheduleName
      },
      {
        name: t('storageConfig'),
        value: generalSettings.storageConfig.label
      }
    ],
    backupObjects: [
      {
        name: t('apiType'),
        value: t(apiType, { keyPrefix: 'backup' })
      },
      {
        name: t(dbType),
        value: backupObjects.keyspace?.isDefaultOption
          ? t(apiType === 'Ysql' ? 'allDatabases' : 'allKeyspaces', { keyPrefix: 'backup' })
          : backupObjects.keyspace?.label
      },
      {
        name: t('tables'),
        value: tablesList
      }
    ],
    backupFrequency: [
      {
        name: t('backupStrategy'),
        value: t(backupFrequency.backupStrategy)
      },
      {
        name: t('fullBackup'),
        value: fullBackup
      },
      {
        name: t('incrementalBackup'),
        value: backupFrequency.useIncrementalBackup
          ? `${t('every', {
            keyPrefix: 'backup.scheduled.create.backupFrequency.backupFrequencyField'
          })} ${backupFrequency.incrementalBackupFrequency} ${backupFrequency.incrementalBackupFrequencyTimeUnit
          }`
          : '-'
      },
      {
        name: t('retentionPeriod'),
        value: backupFrequency.keepIndefinitely
          ? t('wontExpire')
          : `${backupFrequency.expiryTime} ${backupFrequency.expiryTimeUnit}`
      }
    ]
  };

  return (
    <div className={classes.root}>
      <Typography variant="h5">{t('title')}</Typography>
      <div className={classes.summary}>
        <table className={classes.table}>
          <tbody>
            {values(summaries).map((summary, ind) => {
              return (
                <Fragment key={ind}>
                  {summary.map((item, index) => (
                    <tr key={index}>
                      <td>{item.name}</td>
                      <td>{item.value}</td>
                    </tr>
                  ))}
                  <tr>
                    <td colSpan={2}>
                      <Divider className={classes.divider} />
                    </td>
                  </tr>
                </Fragment>
              );
            })}
          </tbody>
        </table>
      </div>
      <YBAlert
        variant={AlertVariant.Info}
        text={<Trans t={t} i18nKey={'backupWillStart'} components={{ b: <b /> }} />}
        open
      />
      <div className={classes.helpText}>
        <img src={BulbIcon} alt="--" height={'32px'} width={'32px'} />
        <div>
          <Trans t={t} i18nKey={'helpText'} components={{ br: <br />, a: <a /> }} />
        </div>
      </div>
    </div>
  );
});

BackupSummary.displayName = 'BackupSummary';

export default BackupSummary;
