/*
 * Created on Mon Jun 26 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { makeStyles } from '@material-ui/core';
import { IGeneralSettings } from './GeneralSettings';
import { YBInputField } from '../../../../../../redesign/components';

const useStyles = makeStyles((theme) => ({
  parallelThreads: {
    width: '200px'
  }
}));

const ParallelThreadsConfig = () => {
  const { t } = useTranslation();
  const classes = useStyles();
  const { control } = useFormContext<IGeneralSettings>();
  return (
    <YBInputField
      control={control}
      name="parallelThreads"
      label={t('newRestoreModal.generalSettings.parallelThreads.label')}
      type="number"
      className={classes.parallelThreads}
      inputProps={{ min: 1 }}
    />
  );
};

export default ParallelThreadsConfig;
