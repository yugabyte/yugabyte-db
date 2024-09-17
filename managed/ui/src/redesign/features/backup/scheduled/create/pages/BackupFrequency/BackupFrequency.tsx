/*
 * Created on Thu Aug 08 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { forwardRef, useContext, useImperativeHandle } from 'react';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { useForm } from 'react-hook-form';
import { yupResolver } from '@hookform/resolvers/yup';
import { Divider, makeStyles } from '@material-ui/core';

import { BackupFrequencyModel } from '../../models/IBackupFrequency';
import { BackupStrategy } from './BackupStrategy';
import BackupFrequencyField from './BackupFrequencyField';
import { BackupRetentionField } from './BackupRetentionField';
import { getValidationSchema } from './BackupFrequencyValidation';
import { api, QUERY_KEY } from '../../../../../universe/universe-form/utils/api';
import { RunTimeConfigEntry } from '../../../../../universe/universe-form/utils/dto';
import {
  Page,
  PageRef,
  ScheduledBackupContext,
  ScheduledBackupContextMethods
} from '../../models/ScheduledBackupContext';

const useStyles = makeStyles(() => ({
  root: {
    padding: '24px',
    display: 'flex',
    gap: '24px',
    flexDirection: 'column'
  },
  '@global': {
    'body>div#menu-expiryTimeUnit': {
      zIndex: '99999 !important',
      '& .MuiListSubheader-sticky': {
        top: 'auto'
      }
    }
  }
}));

const BackupFrequency = forwardRef<PageRef>((_, forwardRef) => {
  const [scheduledBackupContext, { setPage, setBackupFrequency }] = (useContext(
    ScheduledBackupContext
  ) as unknown) as ScheduledBackupContextMethods;

  const classes = useStyles();

  const { t } = useTranslation('translation', {
    keyPrefix: 'backup.scheduled.create.backupFrequency'
  });

  const { data: runtimeConfigs } = useQuery([QUERY_KEY.fetchCustomerRunTimeConfigs], () =>
    api.fetchRunTimeConfigs(true)
  );

  const minIncrementalScheduleFrequencyInSecs = runtimeConfigs?.configEntries?.find(
    (c: RunTimeConfigEntry) => c.key === 'yb.backup.minIncrementalScheduleFrequencyInSecs'
  )?.value;

  const { control, handleSubmit } = useForm<BackupFrequencyModel>({
    defaultValues: scheduledBackupContext.formData.backupFrequency,
    resolver: yupResolver(
      getValidationSchema(scheduledBackupContext, t, {
        minIncrementalScheduleFrequencyInSecs: minIncrementalScheduleFrequencyInSecs ?? ''
      })
    )
  });

  useImperativeHandle(
    forwardRef,
    () => ({
      onNext: () => {
        handleSubmit((values) => {
          setBackupFrequency(values);
          setPage(Page.BACKUP_SUMMARY);
        })();
      },
      onPrev: () => {
        setPage(Page.BACKUP_OBJECTS);
      }
    }),
    []
  );

  return (
    <div className={classes.root}>
      <BackupStrategy control={control} name="backupStrategy" />
      <Divider />
      <BackupFrequencyField control={control} />
      <Divider />
      <BackupRetentionField control={control} />
    </div>
  );
});

BackupFrequency.displayName = 'Backup Frequency';
export default BackupFrequency;
