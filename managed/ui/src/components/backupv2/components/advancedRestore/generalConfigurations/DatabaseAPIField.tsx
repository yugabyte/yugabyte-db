/*
 * Created on Tue Jan 02 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import Select from 'react-select';
import { useTranslation } from 'react-i18next';
import { Controller, useFormContext } from 'react-hook-form';
import { makeStyles } from '@material-ui/core';
import { YBLabel } from '../../../../common/descriptors';
import { AdvancedGeneralConfigs } from './GeneralConfigurations';
import { BACKUP_API_TYPES } from '../../../common/IBackup';

const useStyles = makeStyles(() => ({
  root: {
    width: '180px'
  }
}));

const DatabaseAPIField = () => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'advancedRestoreModal.generalConfig'
  });
  const { control } = useFormContext<AdvancedGeneralConfigs>();
  const classes = useStyles();
  return (
    <div className={classes.root}>
      <Controller
        control={control}
        name="apiType"
        render={({ field: { value, onChange, onBlur }, fieldState: { error } }) => (
          <YBLabel
            label={t('selectDBType')}
            meta={{
              touched: !!error?.message,
              error: error?.message
            }}
          >
            <Select
              options={Object.keys(BACKUP_API_TYPES).map((t) => {
                return { value: BACKUP_API_TYPES[t], label: t };
              })}
              onChange={onChange}
              onBlur={onBlur}
              value={value}
              styles={{
                singleValue: (props: any) => {
                  return { ...props, display: 'flex' };
                }
              }}
            />
          </YBLabel>
        )}
      />
    </div>
  );
};

export default DatabaseAPIField;
