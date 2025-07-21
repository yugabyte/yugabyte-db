import { useContext } from 'react';
import { find } from 'lodash';
import { useFormContext } from 'react-hook-form';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { Box, MenuItem, makeStyles } from '@material-ui/core';
import { YBSelectField } from '@yugabyte-ui-library/core';
import {
  ImageBundleDefaultTag,
  ImageBundleYBActiveTag
} from '@app/components/configRedesign/providerRedesign/components/linuxVersionCatalog/LinuxVersionUtils';
import { InstanceSettingProps } from '../../steps/hardware-settings/dtos';
import { CreateUniverseContext, CreateUniverseContextMethods } from '../../CreateUniverseContext';
import { QUERY_KEY, api } from '@app/redesign/features/universe/universe-form/utils/api';
import { ImageBundleType } from '@app/redesign/features/universe/universe-form/utils/dto';
import { LINUX_VERSION_FIELD } from '../FieldNames';

import { ReactComponent as YBLogo } from '@app/redesign/assets/yb-logo-transparent.svg';
import { ReactComponent as StarLogo } from '@app/redesign/assets/in-use-star.svg';

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
  },
  logoStyle: {
    height: 20,
    width: 20,
    padding: 0,
    display: 'flex',
    justifyContent: 'center',
    marginLeft: 8
  },
  defaultChip: {
    height: 20,
    display: 'flex',
    marginLeft: 8
  }
}));

export const LinuxVersionField = ({ disabled }: { disabled: boolean }) => {
  const { watch, control, setValue } = useFormContext<InstanceSettingProps>();
  const { t } = useTranslation('translation', { keyPrefix: 'universeForm.instanceConfig' });
  const linuxMenuStyles = menuStyles();

  const [{ generalSettings }] = (useContext(
    CreateUniverseContext
  ) as unknown) as CreateUniverseContextMethods;

  const cpuArch = watch('arch');
  const provider = generalSettings?.providerConfiguration;
  const fieldValue = watch(LINUX_VERSION_FIELD);

  const { data: linuxVersions } = useQuery(
    [QUERY_KEY.getLinuxVersions, provider?.uuid, cpuArch],
    () => api.getLinuxVersions(provider?.uuid ?? '', cpuArch),
    {
      enabled: !!provider,
      onSuccess(data) {
        if (!fieldValue && data.length) {
          const defaultImg = find(data, { useAsDefault: true });
          if (defaultImg) {
            setValue(LINUX_VERSION_FIELD, defaultImg?.uuid, { shouldValidate: true });
          }
        }
      }
    }
  );

  return (
    <Box display="flex" width="100%" data-testid="linuxVersion-Container">
      <Box flex={1}>
        <YBSelectField
          fullWidth
          name={LINUX_VERSION_FIELD}
          disabled={disabled}
          className={linuxMenuStyles.selectedValue}
          control={control}
          label={t('linuxVersion')}
          dataTestId="linux-version-field"
        >
          {linuxVersions?.map((version) => (
            <MenuItem
              key={version.uuid}
              value={version.uuid}
              className={linuxMenuStyles.menuItemContainer}
            >
              {version.name}
              {version.metadata?.type === ImageBundleType.YBA_ACTIVE && (
                <ImageBundleYBActiveTag icon={<YBLogo />} className={linuxMenuStyles.logoStyle} />
              )}
              {version.useAsDefault && (
                <ImageBundleDefaultTag
                  icon={<StarLogo />}
                  className={linuxMenuStyles.defaultChip}
                />
              )}
            </MenuItem>
          ))}
        </YBSelectField>
      </Box>
    </Box>
  );
};
