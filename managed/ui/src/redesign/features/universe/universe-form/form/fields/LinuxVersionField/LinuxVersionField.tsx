/*
 * Created on Mon Nov 13 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { Controller, useFormContext, useWatch } from 'react-hook-form';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { Box, MenuItem, makeStyles } from '@material-ui/core';
import { YBLabel, YBSelectField } from '../../../../../../components';
import {
  ImageBundleDefaultTag,
  ImageBundleYBActiveTag
} from '../../../../../../../components/configRedesign/providerRedesign/components/linuxVersionCatalog/LinuxVersionUtils';
import { useFormFieldStyles } from '../../../universeMainStyle';
import { LINUX_VERSION_FIELD, PROVIDER_FIELD } from '../../../utils/constants';
import { ImageBundleType, UniverseFormData } from '../../../utils/dto';
import { QUERY_KEY, api } from '../../../utils/api';

const menuStyles = makeStyles(() => ({
  menuItemContainer: {
    height: '48px',
    display: 'flex',
    gap: '12px'
  },
  selectedValue: {
    '& .MuiSelect-select': {
      gap: '14px'
    }
  }
}));

export const LinuxVersionField = ({ disabled }: { disabled: boolean }) => {
  const {
    control,
    setValue,
    getValues,
    formState: { errors }
  } = useFormContext<UniverseFormData>();
  const { t } = useTranslation('translation', { keyPrefix: 'universeForm.instanceConfig' });
  const classes = useFormFieldStyles();
  const linuxMenuStyles = menuStyles();

  const cpuArch = getValues()?.instanceConfig?.arch;
  const provider = useWatch({ name: PROVIDER_FIELD });

  const { data: linuxVersions } = useQuery(
    [QUERY_KEY.getLinuxVersions, provider?.uuid, cpuArch],
    () => api.getLinuxVersions(provider?.uuid, cpuArch),
    {
      enabled: !!provider
    }
  );

  return (
    <Controller
      name={LINUX_VERSION_FIELD}
      control={control}
      rules={{
        required: t('validation.required', {
          field: t('linuxVersion'),
          keyPrefix: 'universeForm'
        }) as string
      }}
      render={({ field }) => {
        return (
          <Box display="flex" width="100%" data-testid="linuxVersion-Container">
            <YBLabel dataTestId="linuxVersion-Label">{t('linuxVersion')}</YBLabel>
            <Box flex={1} className={classes.defaultTextBox}>
              <YBSelectField
                fullWidth
                name={LINUX_VERSION_FIELD}
                value={field.value}
                inputProps={{
                  'data-testid': `linuxVersion`
                }}
                disabled={disabled}
                onChange={(e) => {
                  setValue(LINUX_VERSION_FIELD, e.target.value, {
                    shouldValidate: true
                  });
                }}
                className={linuxMenuStyles.selectedValue}
              >
                {linuxVersions?.map((version) => (
                  <MenuItem
                    key={version.uuid}
                    value={version.uuid}
                    className={linuxMenuStyles.menuItemContainer}
                  >
                    {version.name}
                    {version.useAsDefault && <ImageBundleDefaultTag />}
                    {version.metadata?.type === ImageBundleType.YBA_ACTIVE && (
                      <ImageBundleYBActiveTag />
                    )}
                  </MenuItem>
                ))}
              </YBSelectField>
            </Box>
          </Box>
        );
      }}
    />
  );
};
