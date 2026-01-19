import { FC, useContext } from 'react';
import { useFormContext } from 'react-hook-form';
import { Trans, useTranslation } from 'react-i18next';
import { styled, Typography } from '@material-ui/core';
import { AlertVariant, YBAlert, YBButton, YBTag } from '@yugabyte-ui-library/core';
import { Zone } from './Zone';
import { CreateUniverseContext, CreateUniverseContextMethods } from '../../CreateUniverseContext';
import { FaultToleranceType } from '../resilence-regions/dtos';
import { NodeAvailabilityProps, Zone as ZoneType } from './dtos';
import { Region } from '../../../../../features/universe/universe-form/utils/dto';
import { canSelectMultipleRegions } from '../../CreateUniverseUtils';
import AddIcon from '../../../../../assets/add2.svg';
import { getFlagFromRegion } from '../../helpers/RegionToFlagUtils';

interface RegionCardProps {
  region: Region;
  index: number;
}

const StyledRegionCard = styled('div')(({ theme }) => ({
  background: '#FBFCFD',
  display: 'flex',
  flexDirection: 'column',
  gap: '24px',
  border: `1px solid ${theme.palette.grey[300]}`,
  borderRadius: '8px'
}));

export const RegionCard: FC<RegionCardProps> = ({ region, index }) => {
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
  const nodesPerAz = watch('nodeCountPerAz');
  const addAvailabilityZone = () => {
    const azToAdd = region.zones.find((zone) => !az.find((a) => a.name === zone.name));
    if (!azToAdd) return;

    setValue(`availabilityZones.${region.code}`, [
      ...az,
      { ...azToAdd, nodeCount: nodesPerAz ?? 1, preffered: 0 }
    ]);
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
      <div style={{ display: 'flex', gap: '24px', alignItems: 'center', padding: '10px 24px' }}>
        <Typography color="textSecondary" variant="body1">
          {t('region', { region_count: index + 1 })}
        </Typography>
        <YBTag size="medium">
          {getFlagFromRegion(region.code)} {region.name} ({region.code})
        </YBTag>
      </div>
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
            index={i}
            region={region}
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
        <YBButton
          variant="secondary"
          onClick={addAvailabilityZone}
          disabled={
            az.length >= region.zones.length ||
            resilienceAndRegionsSettings?.faultToleranceType === FaultToleranceType.NODE_LEVEL ||
            resilienceAndRegionsSettings?.faultToleranceType === FaultToleranceType.NONE ||
            !canSelectMultipleRegions(resilienceAndRegionsSettings?.resilienceType)
          }
          startIcon={<AddIcon />}
          sx={{ width: '200px', marginLeft: '50px' }}
          dataTestId="add-availability-zone-button"
        >
          {t('add_button')}
        </YBButton>
        {errors.nodeCountPerAz?.message && (
          <YBAlert
            open
            variant={AlertVariant.Error}
            text={
              <Trans
                t={t}
                i18nKey={(errors as any)?.nodeCountPerAz?.message}
                components={{ b: <b /> }}
              >
                {(errors as any).nodeCountPerAz.message}
              </Trans>
            }
          />
        )}
      </div>
    </StyledRegionCard>
  );
};
