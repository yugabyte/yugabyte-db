/*
 * Created on Tue Sep 10 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import cronstrue from 'cronstrue';
import { useTranslation } from 'react-i18next';
import { makeStyles } from '@material-ui/core';
import { YBModal } from '../../../../components';
import { convertMsecToTimeFrame } from '../../../../../components/backupv2/scheduled/ScheduledBackupUtils';
import { IBackupSchedule } from '../../../../../components/backupv2';

interface ScheduledBackupShowIntervalsModalProps {
  schedule: IBackupSchedule;
  visible: boolean;
  onHide: () => void;
}

const useStyles = makeStyles((theme) => ({
  root: {
    padding: '24px'
  },
  panel: {
    width: '100%',
    padding: '16px',
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    background: '#FBFBFB',
    borderRadius: '8px',
    display: 'flex',
    gap: '60px'
  },
  attribute: {
    fontWeight: 500,
    fontSize: '11.5px',
    textTransform: 'uppercase',
    color: theme.palette.grey[600],
    '&>div': {
      marginBottom: '8px'
    }
  },
  value: {
    fontSize: '13px',
    fontWeight: 600,
    color: theme.palette.grey[900]
  }
}));

const ScheduledBackupShowIntervalsModal: FC<ScheduledBackupShowIntervalsModalProps> = ({
  schedule,
  onHide,
  visible
}) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'backup.scheduled.list.showIntervalModal'
  });
  const classes = useStyles();

  let backupInterval = '';

  if (schedule.cronExpression) {
    backupInterval = cronstrue.toString(schedule.cronExpression);
  } else {
    backupInterval = convertMsecToTimeFrame(
      schedule.frequency,
      schedule.frequencyTimeUnit,
      'Every '
    );
  }

  const incrementalBackup =
    schedule.incrementalBackupFrequency === 0
      ? '-'
      : convertMsecToTimeFrame(
          schedule.incrementalBackupFrequency,
          schedule.incrementalBackupFrequencyTimeUnit,
          'Every '
        );

  return (
    <YBModal
      open={visible}
      onClose={onHide}
      overrideHeight={'260px'}
      overrideWidth={'575px'}
      title={t('title')}
      dialogContentProps={{
        dividers: true,
        className: classes.root
      }}
      cancelLabel={t('close', { keyPrefix: 'common' })}
    >
      <div className={classes.panel}>
        <div className={classes.attribute}>
          <div>{t('fullBackup')}</div>
          <div>{t('incrementalBackup')}</div>
        </div>
        <div className={classes.value}>
          <div>{backupInterval}</div>
          <div>{incrementalBackup}</div>
        </div>
      </div>
    </YBModal>
  );
};

export default ScheduledBackupShowIntervalsModal;
