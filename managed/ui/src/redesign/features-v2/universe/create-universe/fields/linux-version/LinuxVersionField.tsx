import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { YBSelectField, mui, YBTooltip } from '@yugabyte-ui-library/core';
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

const { Box, MenuItem, Typography } = mui;

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

  return (
    <YBTooltip
      title={<Typography variant="subtitle1">{t('recommendedVersion')}</Typography>}
      arrow
      placement="top"
    >
      <Box
        sx={{
          width: 26,
          height: 24,
          px: '6px',
          py: '2px',
          backgroundColor: (theme) => theme.palette.grey[200],
          borderRadius: '4px',
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'center',
          ...sx
        }}
      >
        {icon || <YBLogo width={14} height={14} />}
      </Box>
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
      <Typography
        variant="subtitle1"
        component={'span'}
        sx={{
          height: 24,
          px: '6px',
          py: '2px',
          display: 'flex',
          gap: '4px',
          backgroundColor: (theme) => theme.palette.grey[200],
          borderRadius: '4px',
          alignItems: 'center',
          justifyContent: 'center',
          ml: 1,
          ...sx
        }}
      >
        {icon ? icon : <FlagIcon width="18" />}
        {text ? text : t('default')}
      </Typography>
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
      enabled: !!provider?.uuid && !!cpuArch,
      onSuccess(data) {
        const selected = data.find((item) => item.uuid === fieldValue);
        if (!selected && data.length) {
          const defaultImg = data.find((item) => item.useAsDefault);
          setValue(LINUX_VERSION_FIELD, defaultImg?.uuid ?? data[0].uuid, { shouldValidate: true });
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
                <ImageBundleYBActiveTag
                  icon={<YBLogo />}
                  sx={{ height: 20, width: 20, ml: 1, display: 'flex', alignItems: 'center' }}
                />
              )}
              {version.useAsDefault && (
                <ImageBundleDefaultTag
                  icon={<StarLogo />}
                  sx={{ height: 20, ml: 1, display: 'flex', alignItems: 'center' }}
                />
              )}
            </MenuItem>
          ))}
        </YBSelectField>
      </Box>
    </Box>
  );
};
