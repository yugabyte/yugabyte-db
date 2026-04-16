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
  addAzTooltipValues
}) => {
  const {
    control,
    watch,
    setValue,
    formState: { errors }
  } = useFormContext<NodeAvailabilityProps>();

  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.nodesAndAvailability.availabilityZones'
  });

  const az = watch(`availabilityZones.${region.code}`);
  const allAz = watch('availabilityZones');

  const showNodesCountError = shouldMarkNodesFieldError(
    showErrorsAfterSubmit,
    (errors as any)?.lesserNodes?.message
  );
  const addAvailabilityZone = () => {
    const azToAdd = region.zones.find((zone) => !az.find((a) => a.name === zone.name));
    if (!azToAdd) return;

    const existingZones = Object.values(allAz ?? {}).flat() as ZoneType[];
    const nodeCountFromConfig =
      existingZones.find((z) => typeof z.nodeCount === 'number' && z.nodeCount >= 1)?.nodeCount ??
      1;

    const newZone =
      mode === ResilienceFormMode.EXPERT_MODE
        ? { name: '', uuid: '', nodeCount: nodeCountFromConfig, preffered: az.length }
        : { ...azToAdd, nodeCount: nodeCountFromConfig, preffered: az.length };

    setValue(`availabilityZones.${region.code}`, [...az, newZone], { shouldValidate: true });
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
