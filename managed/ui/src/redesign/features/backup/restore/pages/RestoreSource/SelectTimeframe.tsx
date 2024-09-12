/*
 * Created on Tue Aug 20 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { useEffect } from 'react';
import { useFormContext } from 'react-hook-form';
import { useMount } from 'react-use';
import { makeStyles, MenuItem, Typography } from '@material-ui/core';
import { Trans, useTranslation } from 'react-i18next';
import moment from 'moment';

import { YBInputField, YBRadioGroupField, YBSelect } from '../../../../../components';
import { YBTag, YBTag_Types } from '../../../../../../components/common/YBTag';
import {
  doesUserSelectsSingleKeyspaceToRestore,
  getLastSuccessfulIncrementalBackup,
  getMomentFromPITRMillis,
  GetRestoreContext,
  isPITREnabledInBackup,
  userCanSelectTimeFrame
} from '../../RestoreUtils';
import { YBTimePicker } from '../../../../../components/YBTimePicker/YBTimePicker';
import { computePITRRestoreWindow } from './RestoreTimeFrameUtils';
import { ybFormatDate } from '../../../../../helpers/DateUtils';
import { RestoreFormModel, TimeToRestoreType } from '../../models/RestoreFormModel';

const useStyles = makeStyles((theme) => ({
  configureRestoreTime: {
    padding: '24px 16px',
    background: '#FBFBFB',
    borderRadius: '8px',
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    marginTop: '16px',
    display: 'flex',
    flexDirection: 'column',
    gap: '16px'
  },
  configureRestoreTimeFields: {
    display: 'flex',
    alignItems: 'baseline',
    gap: '16px',
    '& .MuiFormLabel-root': {
      fontSize: '13px',
      textTransform: 'capitalize',
      color: theme.palette.ybacolors.labelBackground,
      fontWeight: '400 !important',
      marginBottom: '4px !important'
    }
  },
  helpText: {
    color: theme.palette.ybacolors.textDarkGray,
    marginTop: '16px',
    lineHeight: '20px'
  },
  link: {
    color: theme.palette.ybacolors.textDarkGray,
    textDecoration: 'underline',
    cursor: 'pointer'
  },
  '@global': {
    'body>div#menu-source\\.pitrMillisOptions\\.incBackupTime': {
      zIndex: '99999 !important',
      '& .MuiListSubheader-sticky': {
        top: 'auto'
      }
    },
    'body div.rc-time-picker-panel': {
      zIndex: '99999 !important'
    }
  }
}));

export const SelectTimeframe = () => {
  const { control, watch, setValue, getValues } = useFormContext<RestoreFormModel>();
  const [restoreContext] = GetRestoreContext();

  const { backupDetails, incrementalBackupsData, additionalBackupProps } = restoreContext;

  const classes = useStyles();

  const { t } = useTranslation('translation', {
    keyPrefix: 'backup.restore.source'
  });

  // compute the restore window based on the selected backup
  const restoreTimeWindow = computePITRRestoreWindow(getValues(), restoreContext);
  const userSelectsSingleKeyspaceToRestore = doesUserSelectsSingleKeyspaceToRestore(
    additionalBackupProps!
  );
  const options = [
    {
      label: (
        <>
          {t(restoreTimeWindow.label)}
          <YBTag type={YBTag_Types.YB_GRAY}>
            {backupDetails?.hasIncrementalBackups && !userSelectsSingleKeyspaceToRestore
              ? ybFormatDate(restoreTimeWindow.mostRecentbackup)
              : ybFormatDate(restoreTimeWindow.from)}
          </YBTag>
        </>
      ),
      value: TimeToRestoreType.RECENT_BACKUP
    },
    {
      label: t('earlierPointInTime'),
      value: TimeToRestoreType.EARLIER_POINT_IN_TIME
    }
  ];

  const timeToRestoreType = watch('source.timeToRestoreType');
  const pitrMillis = watch('source.pitrMillisOptions');
  const commonBackupInfo = watch('currentCommonBackupInfo');

  // when the time to restore type changes, we need to update the pitrMillis value
  // which can be used by the restore summary and restore final step
  useEffect(() => {
    const restoreWindow = computePITRRestoreWindow(getValues(), restoreContext);

    if (timeToRestoreType === TimeToRestoreType.RECENT_BACKUP) {
      // no pitr and no inc backup
      if (restoreWindow.label === '') {
        setValue('pitrMillis', 0);
      }

      return setValue(
        'pitrMillis',
        restoreWindow.mostRecentbackup !== ''
          ? moment(restoreWindow.mostRecentbackup).valueOf()
          : moment(restoreWindow.from).valueOf()
      );
    } else {
      //selected from incremental backup's list
      if (!isPITREnabledInBackup(commonBackupInfo!)) {
        return setValue('pitrMillis', moment(pitrMillis.incBackupTime).valueOf());
      }
      // pitr
      return setValue('pitrMillis', getMomentFromPITRMillis(pitrMillis).valueOf());
    }
  }, [pitrMillis.date, pitrMillis.secs, pitrMillis.time, timeToRestoreType, commonBackupInfo]);

  // if pitr is not enabled and has incremental backup , set the default value for incremental backup
  useMount(() => {
    if (!isPITREnabled && backupDetails?.hasIncrementalBackups) {
      const inc = getLastSuccessfulIncrementalBackup(incrementalBackupsData ?? []);
      inc && setValue('source.pitrMillisOptions.incBackupTime', inc.createTime);
    }
  });

  // incremental backup list is based on the creation time. If selected , search for the inc backup and set the value
  const incBackupChanged = (creationDate: string) => {
    const backup = incrementalBackupsData?.find((backup) => backup.createTime === creationDate);
    backup && setValue('currentCommonBackupInfo', backup);
  };

  if (!backupDetails) return null;

  const isPITREnabled = isPITREnabledInBackup(backupDetails.commonBackupInfo);

  if (!userCanSelectTimeFrame(backupDetails, restoreContext)) {
    return null;
  }

  return (
    <div>
      <YBRadioGroupField
        control={control}
        label={t('selectTime')}
        name="source.timeToRestoreType"
        options={options}
      />
      {timeToRestoreType === TimeToRestoreType.EARLIER_POINT_IN_TIME && (
        <div className={classes.configureRestoreTime}>
          <Typography variant="body2">
            <Trans
              t={t}
              i18nKey="restoreBetweenMsg"
              components={{
                b: <b />,
                from: ybFormatDate(restoreTimeWindow.from),
                to: ybFormatDate(restoreTimeWindow.to)
              }}
            />
          </Typography>
          <div className={classes.configureRestoreTimeFields}>
            {isPITREnabled && (
              <>
                <YBInputField
                  control={control}
                  name="source.pitrMillisOptions.date"
                  type="date"
                  label={t('date')}
                />
                <div>
                  {t('time')}
                  <YBTimePicker
                    control={control}
                    name="source.pitrMillisOptions.time"
                    use12Hours={false}
                    showSecond={false}
                    allowEmpty={false}
                  />
                </div>
                <YBInputField
                  control={control}
                  name="source.pitrMillisOptions.secs"
                  type="number"
                  label={t('secs')}
                />
              </>
            )}
            {!isPITREnabled && backupDetails.hasIncrementalBackups && (
              <YBSelect
                name="source.pitrMillisOptions.incBackupTime"
                style={{ width: '250px' }}
                onChange={(e) => {
                  setValue('source.pitrMillisOptions.incBackupTime', e.target.value, {
                    shouldValidate: true
                  });
                  incBackupChanged(e.target.value);
                }}
                value={pitrMillis.incBackupTime}
              >
                {incrementalBackupsData?.map((backup) => (
                  <MenuItem key={backup.createTime} value={backup.createTime}>
                    {ybFormatDate(backup.createTime)}
                  </MenuItem>
                ))}
              </YBSelect>
            )}
          </div>
          <Typography variant="body2" className={classes.helpText}>
            <Trans
              i18nKey="helpText"
              t={t}
              components={{ b: <b />, a: <a className={classes.link} /> }}
              values={{ to: 'asd' }}
            />
          </Typography>
        </div>
      )}
    </div>
  );
};
