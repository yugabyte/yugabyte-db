/*
 * Created on Wed Dec 15 2025
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import { useFormContext } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { RestoreFormModel } from '../../models/RestoreFormModel';
import { YBCheckboxField } from '../../../../../components';

export const RestoreRolesOption: FC = () => {
  const { control, watch, setValue } = useFormContext<RestoreFormModel>();

  const { t } = useTranslation('translation', {
    keyPrefix: 'backup.restore.target'
  });

  const useRoles = watch('target.useRoles');

  return (
    <div>
      <YBCheckboxField
        label={t('restoreRoles')}
        control={control}
        name="target.useRoles"
        checked={useRoles}
        onChange={(event) => {
          setValue('target.useRoles', event.target.checked);
        }}
      />
    </div>
  );
};
