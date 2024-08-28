/*
 * Created on Wed Jul 17 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { useTranslation } from 'react-i18next';
import { makeStyles, Typography } from '@material-ui/core';
import { YBButton } from '../../../components';
import { BackupDisabledTooltip } from '../../../../components/backupv2/components/BackupEmpty';

import { RbacValidator } from '../../rbac/common/RbacApiPermValidator';
import ScheduleIcon from '../../../assets/schedule.svg';

const useStyles = makeStyles((theme) => ({
  root: {
    width: '100%',
    display: 'flex',
    flexDirection: 'column',
    alignItems: 'center',
    height: '250px',
    background: 'rgba(229, 229, 233, 0.2)',
    border: '1px dashed rgba(103, 102, 108, 0.5)',
    borderRadius: '8px',
    justifyContent: 'center',
    gap: '16px',
    position: 'relative'
  },
  subText: {
    color: theme.palette.ybacolors.textDarkGray
  }
}));

export const ScheduledBackupEmpty = ({
  onActionButtonClick,
  disabled = false,
  hasPerm = true
}: {
  onActionButtonClick: () => void;
  disabled?: boolean;
  hasPerm: boolean;
}) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'backup.scheduled.empty'
  });
  const classes = useStyles();

  return (
    <div className={classes.root}>
      <img alt="scheduleIcon" src={ScheduleIcon} width="48" />
      <Typography className={classes.subText} variant="body2">
        {t('subText')}
      </Typography>
      <RbacValidator customValidateFunction={() => hasPerm} isControl>
        <BackupDisabledTooltip disabled={disabled} hasPerm={hasPerm}>
          <YBButton
            onClick={onActionButtonClick}
            variant="primary"
            size="large"
            disabled={disabled}
            data-testid="create-scheduled-backup"
          >
            {t('createSchduledPolicy')}
          </YBButton>
        </BackupDisabledTooltip>
      </RbacValidator>
    </div>
  );
};
