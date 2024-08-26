/*
 * Created on Wed Aug 14 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { makeStyles } from '@material-ui/core';
import { Control, useController } from 'react-hook-form';
import { useQuery } from 'react-query';
import { find } from 'lodash';

import { YBInputField } from '../../../../../../components';
import { GetUniverseUUID } from '../../../ScheduledBackupUtils';

import { api } from '../../../../../../helpers/api';
import { isYbcEnabledUniverse } from '../../../../../../../utils/UniverseUtils';

import { ParallelThreads } from '../../../../../../../components/backupv2/common/BackupUtils';
import { Universe } from '../../../../../../helpers/dtos';
import { GeneralSettingsModel } from '../../models/IGeneralSettings';

const useStyles = makeStyles(() => ({
  parallelThreads: {
    width: '200px'
  }
}));

interface ParallelThreadsProps {
  control: Control<GeneralSettingsModel>;
}

const ParallelThreadsField: FC<ParallelThreadsProps> = ({ control }) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'backup.scheduled.create.generalSettings'
  });

  const classes = useStyles();

  const universeUUID = GetUniverseUUID();

  const { field: isYBCEnabledInUniverse } = useController({
    control,
    name: 'isYBCEnabledInUniverse'
  });

  const { data: universeInfo, isLoading } = useQuery<Universe>(
    ['fetchUniverse', universeUUID],
    () => api.fetchUniverse(universeUUID!),
    {
      onSuccess(data) {
        isYBCEnabledInUniverse.onChange(isYbcEnabledUniverse(data?.universeDetails));
      }
    }
  );

  const isYBCEnabled = isYbcEnabledUniverse(universeInfo?.universeDetails);

  if (isYBCEnabled || isLoading) return null;

  const primaryCluster = find(universeInfo?.universeDetails?.clusters, { clusterType: 'PRIMARY' });

  const defaultValue = Math.min(
    primaryCluster?.userIntent?.numNodes ?? ParallelThreads.MIN,
    ParallelThreads.MAX
  );

  return (
    <YBInputField
      control={control}
      name="parallelism"
      label={t('parallelThreads')}
      type="number"
      className={classes.parallelThreads}
      defaultValue={defaultValue}
      inputProps={{ min: ParallelThreads.MIN, max: ParallelThreads.MAX }}
      data-testid="parallelism"
    />
  );
};

export default ParallelThreadsField;
