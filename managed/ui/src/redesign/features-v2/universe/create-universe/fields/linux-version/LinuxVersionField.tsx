import { useEffect } from 'react';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { YBSelectField, mui, YBTag, YBTooltip } from '@yugabyte-ui-library/core';
import { QUERY_KEY, api } from '@app/redesign/features/universe/universe-form/utils/api';
import { ImageBundleType } from '@app/redesign/features/universe/universe-form/utils/dto';
import { ProviderType } from '@app/redesign/features-v2/universe/create-universe/steps/general-settings/dtos';
import { InstanceSettingProps } from '@app/redesign/features-v2/universe/create-universe/steps/hardware-settings/dtos';
import {
  LINUX_VERSION_FIELD,
  CPU_ARCH_FIELD
} from '@app/redesign/features-v2/universe/create-universe/fields/FieldNames';

//icons
import YBLogo from '@app/redesign/assets/yb-logo-transparent.svg';
import StarLogo from '@app/redesign/assets/in-use-star.svg';
import FlagIcon from '@app/redesign/assets/flag-secondary.svg';

const { Box, MenuItem, Typography, useTheme } = mui;

const menuProps = {
  anchorOrigin: { vertical: 'bottom', horizontal: 'left' },
  transformOrigin: { vertical: 'top', horizontal: 'left' }
} as const;

export const ImageBundleYBActiveTag = ({
  icon,
  sx
}: {
  icon?: React.ReactChild;
  sx?: mui.SxProps<mui.Theme>;
}) => {
  const { t } = useTranslation('translation', { keyPrefix: 'linuxVersion.form.menuActions' });
  const theme = useTheme();

  return (
    <YBTooltip
      title={<Typography variant="subtitle1">{t('recommendedVersion')}</Typography>}
      arrow
      placement="top"
    >
      <span style={{ display: 'flex', marginLeft: 6, alignItems: 'center' }}>
        <YBTag
          size="small"
          customSx={{
            color: theme.palette.grey[700],
            backgroundColor: theme.palette.grey[200],
            border: 'unset'
          }}
          startIcon={icon || <YBLogo />}
        ></YBTag>
      </span>
    </YBTooltip>
  );
};

export const ImageBundleDefaultTag = ({
  text,
  tooltip,
  icon,
  sx
}: {
  text?: string;
  tooltip?: string;
  icon?: React.ReactChild;
  sx?: mui.SxProps<mui.Theme>;
}) => {
  const { t } = useTranslation('translation', { keyPrefix: 'common' });
  const theme = useTheme();

  return (
    <YBTooltip
      title={
        <Typography variant="subtitle1">
          {tooltip ? tooltip : t('form.menuActions.defaultVersion', { keyPrefix: 'linuxVersion' })}
        </Typography>
      }
      arrow
      placement="top"
    >
      <span style={{ display: 'inline-flex', marginLeft: 6, alignItems: 'center' }}>
        <YBTag
          size="small"
          startIcon={icon !== undefined ? icon : <FlagIcon width="18" />}
          customSx={{
            color: theme.palette.grey[700],
            backgroundColor: theme.palette.grey[200],
            border: 'unset'
          }}
        >
          {text ? text : t('default')}
        </YBTag>
      </span>
    </YBTooltip>
  );
};

export const LinuxVersionField = ({
  disabled,
  provider
}: {
  disabled: boolean;
  provider?: ProviderType;
}) => {
  const { watch, control, setValue } = useFormContext<InstanceSettingProps>();
  const { t } = useTranslation('translation', { keyPrefix: 'createUniverseV2.instanceSettings' });

  const cpuArch = watch(CPU_ARCH_FIELD);
  const fieldValue = watch(LINUX_VERSION_FIELD);

  const { data: linuxVersions } = useQuery(
    [QUERY_KEY.getLinuxVersions, provider?.uuid, cpuArch],
    () => api.getLinuxVersions(provider?.uuid ?? '', cpuArch),
    {
      enabled: !!provider?.uuid && !!cpuArch
    }
  );

  useEffect(() => {
    if (!linuxVersions?.length) return;
    const selected = linuxVersions.find((item) => item.uuid === fieldValue);
    if (!selected) {
      const defaultImg = linuxVersions.find((item) => item.useAsDefault);
      setValue(LINUX_VERSION_FIELD, defaultImg?.uuid ?? linuxVersions[0].uuid, {
        shouldValidate: true
      });
    }
  }, [linuxVersions, fieldValue, setValue]);

  return (
    <Box display="flex" width="100%" data-testid="linuxVersion-Container">
      <Box flex={1}>
        <YBSelectField
          fullWidth
          name={LINUX_VERSION_FIELD}
          disabled={disabled}
          sx={{
            '& .MuiSelect-select': {
              gap: '14px'
            }
          }}
          control={control}
          label={t('linuxVersion')}
          dataTestId="linux-version-field"
          menuProps={menuProps}
        >
          {linuxVersions?.map((version) => (
            <MenuItem
              key={version.uuid}
              value={version.uuid}
              sx={{
                height: '40px',
                display: 'flex',
                alignItems: 'center'
              }}
            >
              {version.name}
              {version.metadata?.type === ImageBundleType.YBA_ACTIVE && (
                <ImageBundleYBActiveTag icon={<YBLogo width={12} height={12} />} />
              )}
              {version.useAsDefault && <ImageBundleDefaultTag icon={<StarLogo />} />}
            </MenuItem>
          ))}
        </YBSelectField>
      </Box>
    </Box>
  );
};
