/*
 * Created on Tue Jan 02 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import Select from 'react-select';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { Controller, useFormContext } from 'react-hook-form';
import { makeStyles } from '@material-ui/core';
import { YBLabel } from '../../../../common/descriptors';
import { AdvancedGeneralConfigs } from './GeneralConfigurations';
import { fetchStorageConfigs } from '../../../common/BackupAPI';

const useStyles = makeStyles(() => ({
  root: {
    width: '100%'
  }
}));

const StorageConfigField = () => {

  const { t } = useTranslation('translation', {
    keyPrefix: 'advancedRestoreModal.generalConfig'
  });

  const { control } = useFormContext<AdvancedGeneralConfigs>();
  
  const classes = useStyles();

  const { data: StorageConfig, isLoading } = useQuery(
    ['storage_configs'],
    () => fetchStorageConfigs(),
    {
      select: (data) => data.data
    }
  );

  return (
    <div className={classes.root}>
      <Controller
        control={control}
        name="backupStorageConfig"
        render={({ field: { value, onChange }, fieldState: { error } }) => (
          <YBLabel
            label={t('storageConfig')}
            meta={{
              touched: !!error?.message,
              error: error?.message
            }}
          >
            <Select
              options={StorageConfig?.map((config) => ({
                value: config.configUUID,
                label: config.configName
              }))}
              onChange={onChange}
              value={value}
              isLoading={isLoading}
              styles={{
                singleValue: (props: any) => {
                  return { ...props, display: 'flex' };
                },
                container: (props) => {
                  return { ...props, width: '100%' };
                }
              }}
            />
          </YBLabel>
        )}
      />
    </div>
  );
};

export default StorageConfigField;
