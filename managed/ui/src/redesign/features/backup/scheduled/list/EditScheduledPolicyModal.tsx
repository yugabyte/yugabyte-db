/*
 * Created on Mon Sep 09 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import { Control, useForm } from 'react-hook-form';
import { useMutation, useQueryClient } from 'react-query';
import { Trans, useTranslation } from 'react-i18next';
import { capitalize } from 'lodash';
import { toast } from 'react-toastify';
import { makeStyles, MenuItem, Typography } from '@material-ui/core';
import { YBModal, YBSelectField } from '../../../../components';
import BackupFrequencyField from '../create/pages/BackupFrequency/BackupFrequencyField';
import { MILLISECONDS_IN } from '../../../../../components/backupv2/scheduled/ScheduledBackupUtils';
import { createErrorMessage } from '../../../../../utils/ObjectUtils';
import { editScheduledBackupPolicy } from '../api/api';
import { IBackupSchedule } from '../../../../../components/backupv2';
import {
  BackupFrequencyModel,
  BackupStrategyType,
  TimeUnit
} from '../create/models/IBackupFrequency';

interface EditScheduledPolicyModalProps {
  visible: boolean;
  onHide: () => void;
  universeUUID: string;
  schedule: IBackupSchedule;
}

interface EditScheduledPolicyModalFormProps extends BackupFrequencyModel {
  scheduledBackupType: BackupStrategyType;
}

const useStyles = makeStyles((theme) => ({
  root: {
    padding: '24px 20px'
  },
  menuItem: {
    height: '72px'
  },
  unorderedList: {
    listStyleType: 'disc',
    color: theme.palette.ybacolors.textDarkGray,
    paddingLeft: '20px',
    marginTop: '8px'
  },
  strategyOptions: {
    display: 'flex',
    flexDirection: 'column'
  },
  selectStrategy: {
    marginBottom: '40px',
    width: '400px'
  }
}));

const EditScheduledPolicyModal: FC<EditScheduledPolicyModalProps> = ({
  visible,
  onHide,
  universeUUID,
  schedule
}) => {
  const { t } = useTranslation('translation', { keyPrefix: 'backup.scheduled.list.editModal' });
  const backupFrequencyKeyPrefix = 'backup.scheduled.create.backupFrequency';
  const classes = useStyles();
  const queryClient = useQueryClient();
  const { control, handleSubmit } = useForm<EditScheduledPolicyModalFormProps>({
    defaultValues: {
      scheduledBackupType: schedule.backupInfo.pointInTimeRestoreEnabled
        ? BackupStrategyType.POINT_IN_TIME
        : BackupStrategyType.STANDARD,
      frequency: schedule.frequency
        ? schedule.frequency / (MILLISECONDS_IN as any)[schedule.frequencyTimeUnit]
        : 0,
      frequencyTimeUnit: capitalize(schedule.frequencyTimeUnit) as TimeUnit,
      useCronExpression: schedule.cronExpression ? true : false,
      cronExpression: schedule.cronExpression,
      useIncrementalBackup: !!schedule.incrementalBackupFrequency,
      incrementalBackupFrequency: schedule.incrementalBackupFrequency
        ? schedule.incrementalBackupFrequency /
          (MILLISECONDS_IN as any)[schedule.incrementalBackupFrequencyTimeUnit]
        : 0,
      incrementalBackupFrequencyTimeUnit: capitalize(
        schedule.incrementalBackupFrequencyTimeUnit
      ) as TimeUnit
    }
  });

  const doEditBackupSchedule = useMutation(
    (values: IBackupSchedule) => editScheduledBackupPolicy(universeUUID, values),
    {
      onSuccess: () => {
        toast.success(t('success'));
        queryClient.invalidateQueries('scheduled_backup_list');
        onHide();
      },
      onError: (error) => {
        toast.error(createErrorMessage(error));
      }
    }
  );

  if (!visible) return null;

  const backupTypeOptions = [
    <MenuItem value={BackupStrategyType.STANDARD} className={classes.menuItem}>
      <div className={classes.strategyOptions}>
        <Typography variant="body1">
          {t('standard.title', { keyPrefix: backupFrequencyKeyPrefix })}
        </Typography>
        <ul className={classes.unorderedList}>
          <Trans
            components={{ li: <li /> }}
            i18nKey={`${backupFrequencyKeyPrefix}.standard.helpText`}
          />
        </ul>
      </div>
    </MenuItem>,
    <MenuItem value={BackupStrategyType.POINT_IN_TIME} className={classes.menuItem}>
      <div className={classes.strategyOptions}>
        <Typography variant="body1">
          {t('pitr.title', { keyPrefix: backupFrequencyKeyPrefix })}
        </Typography>
        <ul className={classes.unorderedList}>
          <Trans
            components={{ li: <li />, a: <a /> }}
            i18nKey={`${backupFrequencyKeyPrefix}.pitr.helpText`}
          />
        </ul>
      </div>
    </MenuItem>
  ];

  return (
    <YBModal
      open={true}
      onClose={onHide}
      title={t('title')}
      overrideWidth={'1000px'}
      overrideHeight={'650px'}
      enableBackdropDismiss
      dialogContentProps={{
        dividers: true,
        className: classes.root
      }}
      submitLabel={t('save', { keyPrefix: 'common' })}
      cancelLabel={t('cancel', { keyPrefix: 'common' })}
      onSubmit={() => {
        handleSubmit((data) => {
          const payload: Record<string, any> = {};
          if (data.useCronExpression) {
            payload['cronExpression'] = data.cronExpression;
          } else {
            payload['frequencyTimeUnit'] = data.frequencyTimeUnit.toUpperCase();
            payload['schedulingFrequency'] =
              (MILLISECONDS_IN as Record<string, any>)[data.frequencyTimeUnit.toUpperCase()] *
              data.frequency;
          }

          if (data.useIncrementalBackup) {
            payload['incrementalBackupFrequency'] =
              (MILLISECONDS_IN as Record<string, any>)[
                data.incrementalBackupFrequencyTimeUnit.toUpperCase()
              ] * data.incrementalBackupFrequency;
            payload[
              'incrementalBackupFrequencyTimeUnit'
            ] = data.incrementalBackupFrequencyTimeUnit.toUpperCase();
          }
          doEditBackupSchedule.mutate({
            scheduleUUID: schedule.scheduleUUID,
            ...payload
          } as any);
        })();
      }}
    >
      <div>
        <YBSelectField
          control={control}
          name="scheduledBackupType"
          label={t('backupStrategy')}
          renderValue={(val) =>
            t(val === BackupStrategyType.STANDARD ? 'standard.title' : 'pitr.title', {
              keyPrefix: backupFrequencyKeyPrefix
            })
          }
          className={classes.selectStrategy}
          disabled
        >
          {...backupTypeOptions}
        </YBSelectField>
        <BackupFrequencyField
          control={(control as unknown) as Control<BackupFrequencyModel>}
          isEditMode
        />
      </div>
    </YBModal>
  );
};

export default EditScheduledPolicyModal;
