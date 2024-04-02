/*
 * Created on Tue Jan 02 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { useFormContext } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { YBCheckboxField, YBInputField } from '../../../../../redesign/components';
import { AdvancedGeneralConfigs } from './GeneralConfigurations';

export const DatabaseNameField = () => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'advancedRestoreModal.generalConfig'
  });
  const { control, watch } = useFormContext<AdvancedGeneralConfigs>();
  const forceKeyspaceRename = watch('forceKeyspaceRename');

  return (
    <div>
      <YBInputField
        control={control}
        name="databaseName"
        label={t('databaseName')}
        placeholder={t('databaseName')}
        fullWidth
        style={{
          marginBottom: '14px'
        }}
      />
      <YBCheckboxField
        control={control}
        name="renameKeyspace"
        label={t('renameDatabase')}
        disabled={forceKeyspaceRename}
      />
    </div>
  );
};
