import { useTranslation } from 'react-i18next';
import { mui } from '@yugabyte-ui-library/core';
import { CloudIconBox } from '../../helpers/CloudIconBox';
import { ProviderType } from '../general-settings/dtos';
import { InstanceSettingProps } from './dtos';
import { archLabelKey } from '../../helpers/instanceByProvider';
import { CloudType } from '@app/redesign/features/universe/universe-form/utils/dto';

const { Box, Typography } = mui;

const labelSx = {
  fontSize: '11.5px',
  fontWeight: 500,
  lineHeight: '16px',
  color: '#6D7C88',
  textTransform: 'uppercase' as const
};

const valueSx = {
  fontSize: '13px',
  fontWeight: 400,
  lineHeight: '16px',
  color: '#0B1117'
};

function Field({
  label,
  value,
  width
}: {
  label: string;
  value: React.ReactNode;
  width?: number | string;
}) {
  return (
    <Box
      sx={{
        display: 'flex',
        flexDirection: 'column',
        gap: '4px',
        minWidth: 0,
        flex: width ? undefined : 1,
        width
      }}
    >
      <Typography sx={labelSx}>{label}</Typography>
      <Box sx={{ py: '4px' }}>
        <Typography sx={valueSx}>{value}</Typography>
      </Box>
    </Box>
  );
}

function linuxBundle(
  provider: ProviderType,
  imageBundleUUID: string | null
): { name: string; isDefault: boolean } | null {
  if (!imageBundleUUID) return null;
  const bundle = provider.imageBundles?.find((img) => img.uuid === imageBundleUUID) as
    | { name?: string; uuid: string; useAsDefault?: boolean }
    | undefined;
  if (!bundle) return { name: imageBundleUUID, isDefault: false };
  return { name: bundle.name ?? imageBundleUUID, isDefault: !!bundle.useAsDefault };
}

function volumeSummary(settings: InstanceSettingProps, isMaster: boolean): string {
  const device = isMaster ? settings.masterDeviceInfo : settings.deviceInfo;
  if (!device?.numVolumes || !device?.volumeSize) return '—';
  return `${device.numVolumes} X ${device.volumeSize} GB`;
}

type Props = {
  provider: ProviderType;
  settings: InstanceSettingProps;
  showMaster?: boolean;
};

export function ProviderInstanceSummaryCard({ provider, settings, showMaster = false }: Props) {
  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.instanceSettings'
  });
  const { t: tMulti } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.instanceSettings.multiProvider'
  });
  const codeLabel = (provider.code ?? '').toUpperCase();
  const isAws = provider.code === CloudType.aws;
  const useMasterFields = showMaster && !settings.keepMasterTserverSame;
  const device = useMasterFields ? settings.masterDeviceInfo : settings.deviceInfo;
  const instanceType = useMasterFields ? settings.masterInstanceType : settings.instanceType;
  const linux = linuxBundle(provider, settings.imageBundleUUID);

  return (
    <Box
      data-testid={`provider-instance-summary-${provider.uuid}`}
      sx={{
        border: '1px solid #D7DEE4',
        borderRadius: '8px',
        backgroundColor: '#FBFCFD',
        display: 'flex',
        flexDirection: 'column',
        width: '100%',
        overflow: 'hidden'
      }}
    >
      <Box
        sx={{
          display: 'flex',
          alignItems: 'center',
          gap: '12px',
          height: '64px',
          px: '24px'
        }}
      >
        <CloudIconBox code={provider.code} size={20} />
        <Typography
          sx={{ fontSize: '13px', fontWeight: 600, lineHeight: '16px', color: '#0B1117' }}
        >
          {codeLabel}
        </Typography>
      </Box>

      <Box
        sx={{
          display: 'flex',
          flexDirection: 'column',
          gap: '16px',
          px: '24px',
          pt: '8px',
          pb: '16px'
        }}
      >
        {showMaster && settings.keepMasterTserverSame ? (
          <Typography sx={{ ...valueSx, color: '#4E5F6D' }}>
            {t('keepMasterTserverSame')}
          </Typography>
        ) : (
          <>
            {/* CPU arch / Linux are global, so master cards omit them (design). */}
            {!useMasterFields && (
              <Box sx={{ display: 'flex', flexDirection: 'column', gap: '16px' }}>
                <Box sx={{ display: 'flex', gap: '87px', alignItems: 'center' }}>
                  <Field label={t('cpuArch')} value={t(archLabelKey(settings.arch))} width={198} />
                  <Box sx={{ display: 'flex', flexDirection: 'column', gap: '4px', minWidth: 0 }}>
                    <Typography sx={labelSx}>{t('linuxVersion')}</Typography>
                    <Box sx={{ display: 'flex', alignItems: 'center', gap: '8px', minWidth: 0 }}>
                      <Typography sx={{ ...valueSx, whiteSpace: 'nowrap' }}>
                        {linux?.name ?? '—'}
                      </Typography>
                      {linux?.isDefault && (
                        <Box
                          sx={{
                            display: 'inline-flex',
                            alignItems: 'center',
                            gap: '4px',
                            px: '6px',
                            py: '2px',
                            borderRadius: '4px',
                            backgroundColor: '#E9EEF2'
                          }}
                        >
                          <Typography
                            sx={{ fontSize: '11.5px', lineHeight: '16px', color: '#4E5F6D' }}
                          >
                            {tMulti('defaultLinux')}
                          </Typography>
                        </Box>
                      )}
                    </Box>
                  </Box>
                </Box>
                <Box sx={{ borderBottom: '1px solid #E9EEF2', width: '100%' }} />
              </Box>
            )}

            <Field label={t('instanceType')} value={instanceType ?? '—'} width={252} />

            <Box
              sx={{
                display: 'flex',
                alignItems: 'flex-start',
                justifyContent: 'space-between',
                gap: 2
              }}
            >
              <Field
                label={tMulti('volumePerNode')}
                value={volumeSummary(settings, useMasterFields)}
              />
              {device?.storageType ? (
                <Field
                  label={isAws ? t('ebs') : tMulti('storage')}
                  value={String(device.storageType).toUpperCase()}
                />
              ) : null}
              {isAws && device?.diskIops != null ? (
                <Field label={t('provisionedIopsPerNode')} value={device.diskIops} width={205} />
              ) : null}
              {isAws && device?.throughput != null ? (
                <Field
                  label={t('provisionedThroughputPerNode')}
                  value={`${device.throughput} ${t('throughputUnit')}`}
                  width={205}
                />
              ) : null}
            </Box>
          </>
        )}
      </Box>
    </Box>
  );
}
