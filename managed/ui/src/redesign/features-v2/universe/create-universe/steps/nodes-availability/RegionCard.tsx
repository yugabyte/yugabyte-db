import { FC, useContext } from 'react';
import { useFormContext } from 'react-hook-form';
import { Trans, useTranslation } from 'react-i18next';
import { AlertVariant, YBAlert, YBButton, YBTag, YBTooltip, mui } from '@yugabyte-ui-library/core';
import { Zone } from './Zone';
import { shouldMarkNodesFieldError } from './lessNodesFieldError';
import { CreateUniverseContext, CreateUniverseContextMethods } from '../../CreateUniverseContext';
import { getFlagFromRegion } from '../../helpers/RegionToFlagUtils';
import { canSelectMultipleRegions, getFaultToleranceNeeded } from '../../CreateUniverseUtils';
import { FaultToleranceType, ResilienceFormMode } from '../resilence-regions/dtos';
import { NodeAvailabilityProps, Zone as ZoneType } from './dtos';
import { Region } from '../../../../../features/universe/universe-form/utils/dto';

//icons
import AddIcon from '../../../../../assets/add2.svg';

interface RegionCardProps {
  region: Region;
  index: number;
  showErrorsAfterSubmit?: boolean;
}

const { styled, Typography, Box, Link } = mui;

const StyledRegionCard = styled('div')(({ theme }) => ({
  background: '#FBFCFD',
  display: 'flex',
  flexDirection: 'column',
  gap: '24px',
  border: `1px solid ${theme.palette.grey[300]}`,
  borderRadius: '8px',
  width: '672px'
}));

const StyledRegionHeader = styled('div')(({ theme }) => ({
  display: 'flex',
  flexDirection: 'row',
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

export const RegionCard: FC<RegionCardProps> = ({ region, index, showErrorsAfterSubmit = true }) => {
  const {
    control,
    watch,
    setValue,
    formState: { errors }
  } = useFormContext<NodeAvailabilityProps>();
  const [{ resilienceAndRegionsSettings }] = (useContext(
    CreateUniverseContext
  ) as unknown) as CreateUniverseContextMethods;

  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.nodesAndAvailability.availabilityZones'
  });

  const az = watch(`availabilityZones.${region.code}`);
  const allAz = watch('availabilityZones');
  const totalAzCount = Object.values(allAz ?? {}).reduce((acc, zones) => acc + zones.length, 0);

  const showNodesCountError = shouldMarkNodesFieldError(
    showErrorsAfterSubmit,
    (errors as any)?.lesserNodes?.message
  );
  const isRegionAzLimitReached = az.length >= region.zones.length;
  const ft = resilienceAndRegionsSettings?.faultToleranceType;
  const displayReplicationFactor = getFaultToleranceNeeded(
    resilienceAndRegionsSettings?.resilienceFactor ?? 1
  );
  const maxTotalAzCount =
    ft === FaultToleranceType.NODE_LEVEL
      ? Math.max(1, displayReplicationFactor - 1)
      : displayReplicationFactor;
  const isGuided = resilienceAndRegionsSettings?.resilienceFormMode === ResilienceFormMode.GUIDED;
  const isTotalAzLimitReached =
    isGuided &&
    (ft === FaultToleranceType.AZ_LEVEL || ft === FaultToleranceType.NODE_LEVEL) &&
    totalAzCount >= maxTotalAzCount;
  const isAddAzDisabled = isRegionAzLimitReached || isTotalAzLimitReached;
  const isAddAzDisabledByAzLevelCap = isTotalAzLimitReached && ft !== FaultToleranceType.NODE_LEVEL;
  const addAzTooltip = isAddAzDisabled
    ? isTotalAzLimitReached
      ? ft === FaultToleranceType.NODE_LEVEL
        ? t('tooltips.addAvailabilityZoneDisabledNodeLevel', {
          max_az: maxTotalAzCount
        })
        : t('tooltips.addAvailabilityZoneDisabled', {
          outage_count: resilienceAndRegionsSettings?.resilienceFactor ?? 1,
          az_count: maxTotalAzCount
        })
      : t('tooltips.regionNoMoreAz', { count: region.zones.length })
    : '';
  const addAvailabilityZone = () => {
    const azToAdd = region.zones.find((zone) => !az.find((a) => a.name === zone.name));
    if (!azToAdd) return;

    const existingZones = Object.values(allAz ?? {}).flat() as ZoneType[];
    const nodeCountFromConfig =
      existingZones.find((z) => typeof z.nodeCount === 'number' && z.nodeCount >= 1)?.nodeCount ??
      1;

    setValue(`availabilityZones.${region.code}`, [
      ...az,
      { ...azToAdd, nodeCount: nodeCountFromConfig, preffered: az.length }
    ], { shouldValidate: true });
  };

  const updatePreferredRanks = (azs: ZoneType[], removedPreferredRank: number) => {
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
      <StyledRegionHeader>
        <Typography color="textSecondary" variant="body1">
          {t('region', { region_count: index + 1 })}
        </Typography>
        <StyledRegionTagContainer>
          <YBTooltip title={<StyledTooltipText>{t('tooltips.regionTag')}</StyledTooltipText>}>
            <span>
              <YBTag size="medium">
                {getFlagFromRegion(region.code)} {region.name} ({region.code})
              </YBTag>
            </span>
          </YBTooltip>
        </StyledRegionTagContainer>
      </StyledRegionHeader>
      <div
        style={{
          padding: '0px 24px 24px 64px',
          display: 'flex',
          flexDirection: 'column',
          gap: '24px'
        }}
      >
        {az.map((_, i) => (
          <Zone
            key={i}
            control={control}
            setValue={setValue}
            index={i}
            region={region}
            regionIndex={index}
            showNodesCountError={showNodesCountError}
            remove={() => {
              const updatedAz = az.filter((_, index) => index !== i);
              const updatedPreferredRank = updatePreferredRanks(updatedAz, az[i].preffered);
              setValue(`availabilityZones.${region.code}`, updatedPreferredRank, {
                shouldValidate: true,
                shouldDirty: true
              });
            }}
          />
        ))}
        {(resilienceAndRegionsSettings?.faultToleranceType === FaultToleranceType.AZ_LEVEL || resilienceAndRegionsSettings?.faultToleranceType === FaultToleranceType.NODE_LEVEL) &&
          canSelectMultipleRegions(resilienceAndRegionsSettings?.resilienceType) && (
            <YBTooltip
              title={
                isAddAzDisabled ? (
                  isAddAzDisabledByAzLevelCap ? (
                    <StyledTooltipText>
                      <Trans
                        t={t}
                        i18nKey="tooltips.addAvailabilityZoneDisabled"
                        values={{
                          outage_count: resilienceAndRegionsSettings?.resilienceFactor ?? 1,
                          az_count: maxTotalAzCount
                        }}
                        components={{
                          br: <br />,
                          a: (
                            <Link
                              href={"#"}
                              rel="noopener noreferrer"
                              underline="always"
                              onClick={(e) => e.stopPropagation()}
                              sx={{ fontSize: '11.5px', lineHeight: '16px' }}
                            />
                          )
                        }}
                      />
                    </StyledTooltipText>
                  ) : (
                    <StyledTooltipText>{addAzTooltip}</StyledTooltipText>
                  )
                ) : (
                  ''
                )
              }
            >
              <span style={{ width: 'fit-content', marginLeft: '34px' }}>
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
          ['errMsg.preferredRankRequired'].includes(
            (errors as any).lesserNodes.message
          ) && (
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
