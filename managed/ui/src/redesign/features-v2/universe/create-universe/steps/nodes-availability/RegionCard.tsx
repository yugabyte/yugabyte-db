import { FC } from 'react';
import { useFormContext } from 'react-hook-form';
import { Trans, useTranslation } from 'react-i18next';
import { AlertVariant, YBAlert, YBButton, YBTag, YBTooltip, mui } from '@yugabyte-ui-library/core';
import { Zone } from './Zone';
import { shouldMarkNodesFieldError } from './lessNodesFieldError';
import { getFlagFromRegion } from '../../helpers/RegionToFlagUtils';
import { NodeAvailabilityProps, Zone as ZoneType } from './dtos';
import { Region } from '../../../../../features/universe/universe-form/utils/dto';
import { ResilienceFormMode } from '../resilence-regions/dtos';
import { AZ_NOT_PREFERRED } from '../../helpers/constants';
import { AVAILABILITY_ZONES_BY_PROVIDER } from '../../fields/FieldNames';
import { flattenZonesList } from '../../helpers/nodesByProvider';
import { CloudIconBox } from '../../helpers/CloudIconBox';

//icons
import AddIcon from '../../../../../assets/add2.svg';

interface RegionCardProps {
  region: Region;
  index: number;
  mode: ResilienceFormMode;
  showErrorsAfterSubmit?: boolean;
  showAddAzButton: boolean;
  isAddAzDisabled: boolean;
  isAddAzDisabledByAzLevelCap: boolean;
  addAzTooltip: string;
  addAzTooltipKey?: string;
  addAzTooltipValues?: Record<string, unknown>;
  /** True when the region was not in the original universe placement. */
  isNewRegion?: boolean;
  /**
   * Original universe zone UUIDs for this region (edit placement).
   * Omit / undefined when not in edit-placement baseline mode.
   */
  baselineZoneUuids?: string[];
  /** Multi-provider: nest zone rows under availabilityZonesByProvider[uuid]. */
  providerUuid?: string;
  /** Multi-provider: cloud code for the provider logo on the card header. */
  providerCode?: string;
}

const { styled, Typography, Box, Link, useTheme } = mui;

const StyledRegionCard = styled('div')(({ theme }) => ({
  background: '#FBFCFD',
  display: 'flex',
  flexDirection: 'column',
  gap: '24px',
  border: `1px solid ${theme.palette.grey[300]}`,
  borderRadius: '8px',
  boxSizing: 'border-box',
  width: '672px',
  maxWidth: '100%'
}));

const StyledRegionHeader = styled('div')(({ theme }) => ({
  display: 'flex',
  flexDirection: 'row',
  height: '64px',
  padding: theme.spacing(1.25, 3),
  gap: theme.spacing(3),
  alignItems: 'center'
}));

const StyledRegionTagContainer = styled(Box)(({ theme }) => ({
  display: 'flex',
  flexDirection: 'row',
  height: '40px',
  padding: '8px 6px',
  alignItems: 'center'
}));

const StyledTooltipText = styled('div')(({ theme }) => ({
  fontSize: '11.5px',
  lineHeight: '16px',
  fontWeight: 400,
  color: theme.palette.grey[700]
}));

export const RegionCard: FC<RegionCardProps> = ({
  region,
  index,
  mode,
  showErrorsAfterSubmit = true,
  showAddAzButton,
  isAddAzDisabled,
  isAddAzDisabledByAzLevelCap,
  addAzTooltip,
  addAzTooltipKey,
  addAzTooltipValues,
  isNewRegion = false,
  baselineZoneUuids,
  providerUuid,
  providerCode
}) => {
  const {
    control,
    watch,
    setValue,
    formState: { errors }
  } = useFormContext<NodeAvailabilityProps>();

  const theme = useTheme();

  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.nodesAndAvailability.availabilityZones'
  });

  const regionZonesPath = providerUuid
    ? (`${AVAILABILITY_ZONES_BY_PROVIDER}.${providerUuid}.${region.code}` as const)
    : (`availabilityZones.${region.code}` as const);
  const az = watch(regionZonesPath as any) as ZoneType[] | undefined;
  const allAz = providerUuid
    ? flattenZonesList(watch(AVAILABILITY_ZONES_BY_PROVIDER))
    : (Object.values(watch('availabilityZones') ?? {}).flat() as ZoneType[]);

  const showNodesCountError = shouldMarkNodesFieldError(
    showErrorsAfterSubmit,
    (errors as any)?.lesserNodes?.message
  );

  const isNewAz = (zone: ZoneType | undefined) => {
    if (baselineZoneUuids === undefined || isNewRegion) return false;
    if (!zone?.uuid) return true;
    return !baselineZoneUuids.includes(zone.uuid);
  };
  const addAvailabilityZone = () => {
    const azToAdd = region.zones.find((zone) => !(az ?? []).find((a) => a.name === zone.name));
    if (!azToAdd) return;

    const existingZones = allAz;
    const nodeCountFromConfig =
      existingZones.find((z) => typeof z.nodeCount === 'number' && z.nodeCount >= 1)?.nodeCount ??
      1;
    const newZone =
      mode === ResilienceFormMode.EXPERT_MODE
        ? {
            name: '',
            uuid: '',
            nodeCount: nodeCountFromConfig,
            preffered: AZ_NOT_PREFERRED
          }
        : { ...azToAdd, nodeCount: nodeCountFromConfig, preffered: AZ_NOT_PREFERRED };

    setValue(regionZonesPath as any, [...(az ?? []), newZone], { shouldValidate: true });
  };

  const updatePreferredRanks = (azs: ZoneType[], removedPreferredRank: number) => {
    if (removedPreferredRank === AZ_NOT_PREFERRED) {
      return azs;
    }
    const updatedAz = azs.map((zone) => {
      if (zone.preffered > removedPreferredRank) {
        return { ...zone, preffered: zone.preffered - 1 };
      }
      return zone;
    });
    return updatedAz;
  };

  return (
    <StyledRegionCard>
      {isNewRegion ? (
        <Box sx={{ px: 3, pt: 1.25, pb: 0, mb: -1 }} data-testid={`region-card-new-badge-${index}`}>
          <YBTag size="small" variant="light" color="gradient">
            {t('newRegionBadge')}
          </YBTag>
        </Box>
      ) : null}
      <StyledRegionHeader>
        <Box
          sx={{
            display: 'flex',
            alignItems: 'center',
            gap: '8px',
            height: '20px'
          }}
        >
          {providerCode ? <CloudIconBox code={providerCode} size={24} /> : null}
          <Typography color="textSecondary" variant="body1" sx={{ fontWeight: 600 }}>
            {t('region', { region_count: index + 1 })}
          </Typography>
        </Box>
        <StyledRegionTagContainer>
          <YBTooltip title={<StyledTooltipText>{t('tooltips.regionTag')}</StyledTooltipText>}>
            <span>
              <YBTag
                size="medium"
                customSx={{
                  backgroundColor: theme.palette.primary[200],
                  color: theme.palette.grey[900]
                }}
              >
                {getFlagFromRegion(region.code)} {region.name} ({region.code})
              </YBTag>
            </span>
          </YBTooltip>
        </StyledRegionTagContainer>
      </StyledRegionHeader>
      <div
        style={{
          padding: '0px 24px 24px 96px',
          display: 'flex',
          flexDirection: 'column',
          gap: '24px'
        }}
      >
        {(az ?? []).map((zone, i) => (
          <Box key={i} sx={{ display: 'flex', flexDirection: 'column', gap: 1 }}>
            {isNewAz(zone) ? (
              <Box
                sx={{ ml: '40px' }}
                data-testid={`region-card-new-az-badge-${index}-${i}`}
              >
                <YBTag size="small" variant="light" color="gradient">
                  {t('newRegionBadge')}
                </YBTag>
              </Box>
            ) : null}
            <Zone
              control={control}
              setValue={setValue}
              index={i}
              region={region}
              regionIndex={index}
              providerUuid={providerUuid}
              showNodesCountError={showNodesCountError}
              remove={() => {
                const updatedAz = az.filter((_, index) => index !== i);
                const updatedPreferredRank = updatePreferredRanks(updatedAz, az[i].preffered);
                setValue(regionZonesPath as any, updatedPreferredRank, {
                  shouldValidate: true,
                  shouldDirty: true
                });
              }}
            />
          </Box>
        ))}
        {showAddAzButton && (
          <YBTooltip
            title={
              isAddAzDisabled ? (
                isAddAzDisabledByAzLevelCap ? (
                  <StyledTooltipText>
                    {addAzTooltipKey ? (
                      <Trans
                        t={t}
                        i18nKey={addAzTooltipKey}
                        values={addAzTooltipValues}
                        components={{
                          br: <br />,
                          a: (
                            <Link
                              href={'#'}
                              rel="noopener noreferrer"
                              underline="always"
                              onClick={(e) => e.stopPropagation()}
                              sx={{ fontSize: '11.5px', lineHeight: '16px' }}
                            />
                          )
                        }}
                      />
                    ) : (
                      addAzTooltip
                    )}
                  </StyledTooltipText>
                ) : (
                  <StyledTooltipText>{addAzTooltip}</StyledTooltipText>
                )
              ) : (
                ''
              )
            }
          >
            <span style={{ width: 'fit-content', marginLeft: '40px' }}>
              <YBButton
                variant="secondary"
                onClick={addAvailabilityZone}
                disabled={isAddAzDisabled}
                startIcon={<AddIcon />}
                sx={{ width: 'fit-content' }}
                dataTestId="add-availability-zone-button"
              >
                {t('add_button')}
              </YBButton>
            </span>
          </YBTooltip>
        )}
        {showErrorsAfterSubmit &&
          index === 0 &&
          (errors as any)?.lesserNodes?.message &&
          ['errMsg.preferredRankRequired'].includes((errors as any).lesserNodes.message) && (
            <YBAlert
              open
              variant={AlertVariant.Error}
              text={
                <Trans
                  t={t}
                  i18nKey={(errors as any)?.lesserNodes?.message}
                  components={{ b: <b /> }}
                >
                  {(errors as any).lesserNodes.message}
                </Trans>
              }
            />
          )}
      </div>
    </StyledRegionCard>
  );
};
