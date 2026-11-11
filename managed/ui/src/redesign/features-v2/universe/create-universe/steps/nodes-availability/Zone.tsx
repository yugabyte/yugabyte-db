import { FC, useContext, useMemo, useState } from 'react';
import { values } from 'lodash';
import { Trans, useTranslation } from 'react-i18next';
import { Control, Controller, UseFormSetValue, useFormContext, useWatch } from 'react-hook-form';
import { YBInput, YBSelect, YBTooltip, mui } from '@yugabyte-ui-library/core';
import { PreferredInfoModal } from './index';
import { CreateUniverseContext, CreateUniverseContextMethods } from '../../CreateUniverseContext';
import { NodeAvailabilityProps } from './dtos';
import { Region } from '../../../../../features/universe/universe-form/utils/dto';
import { FaultToleranceType, ResilienceFormMode } from '../resilence-regions/dtos';

//icons
import { HelpOutline } from '@material-ui/icons';
import Return from '../../../../../assets/tree.svg';
import RemoveIcon from '../../../../../assets/close-large.svg';
import BookIcon from '../../../../../assets/blue-book.svg';
import BulbIcon from '../../../../../assets/bulb.svg';
import { AZ_NOT_PREFERRED, AZ_PREFFERED_HIGHEST_RANK } from '../../helpers/constants';
import { CloudType } from '@app/redesign/helpers/dtos';
import { getPrimaryProvider } from '../../helpers/providerConfigurations';
import { getEffectiveRegions } from '../../helpers/placementsByProvider';
import { AVAILABILITY_ZONES_BY_PROVIDER } from '../../fields/FieldNames';
import {
  flattenAvailabilityZonesByProvider,
  type AvailabilityZonesByProvider
} from '../../helpers/nodesByProvider';

const { MenuItem, Typography, IconButton, Link, styled, Divider } = mui;

interface ZoneProps {
  control: Control<NodeAvailabilityProps>;
  index: number;
  region: Region;
  remove: () => void;
  regionIndex?: number;
  setValue: UseFormSetValue<NodeAvailabilityProps>;
  /** When NODE_LEVEL total nodes are below RF (lessNodes / lessNodesDedicated). */
  showNodesCountError?: boolean;
  /** Multi-provider: nest zone field paths under availabilityZonesByProvider[uuid]. */
  providerUuid?: string;
}

const menuProps = {
  anchorOrigin: {
    vertical: 'bottom',
    horizontal: 'left'
  },
  transformOrigin: {
    vertical: 'top',
    horizontal: 'left'
  }
} as any;

const StyledTooltipText = styled('div')(({ theme }) => ({
  fontSize: '11.5px',
  lineHeight: '16px',
  fontWeight: 400,
  color: theme.palette.grey[700]
}));

const StyledTooltipFooter = styled('div')(({ theme }) => ({
  marginTop: '8px',
  display: 'flex',
  alignItems: 'center',
  gap: '4px',
  color: theme.palette.primary[600]
}));

const StyledPreferedMenuItem = mui.styled(MenuItem)(({ theme }) => ({
  display: 'flex',
  flexDirection: 'column',
  padding: '8 16px !important',
  gap: '8px',
  height: '56px',
  justifyContent: 'flex-start',
  alignItems: 'flex-start',
  width: '300px',
  '.MuiTypography-subtitle1': {
    color: theme.palette.grey[700]
  }
}));

const StyledRankingHintMenuItem = styled(MenuItem)(({ theme }) => ({
  display: 'flex',
  flexDirection: 'row',
  alignItems: 'center',
  gap: '8px',
  padding: '4px 16px !important',
  height: '32px',
  width: '300px',
  cursor: 'default',
  pointerEvents: 'none',
  opacity: '1 !important',
  color: theme.palette.grey[700],
  fontSize: '11.5px',
  lineHeight: '16px',
  fontWeight: 400,
  '&.Mui-disabled': {
    opacity: 1
  },
  '& svg': {
    flexShrink: 0,
    width: 24,
    height: 24,
    '& path': {
      fill: theme.palette.grey[700]
    }
  }
}));

export const Zone: FC<ZoneProps> = ({
  control,
  index,
  region,
  remove,
  regionIndex,
  setValue,
  showNodesCountError = false,
  providerUuid
}) => {
  const {
    formState: { errors, isSubmitted }
  } = useFormContext<NodeAvailabilityProps>();
  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.nodesAndAvailability.availabilityZones'
  });
  const { t: tCommon } = useTranslation('translation', { keyPrefix: 'common' });
  const [{ resilienceAndRegionsSettings, generalSettings, placementsByProvider }] = useContext(
    CreateUniverseContext
  ) as unknown as CreateUniverseContextMethods;
  const isK8s =
    generalSettings?.cloud === CloudType.kubernetes ||
    getPrimaryProvider(generalSettings)?.code === CloudType.kubernetes;
  const isPrefferedAllowed = ![FaultToleranceType.NODE_LEVEL, FaultToleranceType.NONE].includes(
    resilienceAndRegionsSettings!.faultToleranceType
  );

  const [showPreferredInfoModal, setShowPreferredInfoModal] = useState(false);

  const singularAvailabilityZones = useWatch({ name: 'availabilityZones', control });
  const availabilityZonesByProvider = useWatch({
    name: AVAILABILITY_ZONES_BY_PROVIDER,
    control
  }) as AvailabilityZonesByProvider | undefined;
  const useDedicatedNodes = useWatch({ name: 'useDedicatedNodes', control });

  const availabilityZones = providerUuid
    ? flattenAvailabilityZonesByProvider(availabilityZonesByProvider)
    : singularAvailabilityZones;
  const regionZonesPath = providerUuid
    ? `${AVAILABILITY_ZONES_BY_PROVIDER}.${providerUuid}.${region.code}`
    : `availabilityZones.${region.code}`;
  const zonePath = `${regionZonesPath}.${index}`;

  const selectedAzCountInRegion = providerUuid
    ? availabilityZonesByProvider?.[providerUuid]?.[region.code]?.length ?? 0
    : singularAvailabilityZones?.[region.code]?.length ?? 0;
  const selectedAzNames = useMemo(
    () =>
      new Set(
        values(availabilityZones ?? {})
          .flat()
          .map((selectedZone) => selectedZone.name)
          .filter(Boolean)
      ),
    [availabilityZones]
  );

  const zonesCount = useMemo(
    () => Object.keys(availabilityZones ?? {}).reduce((a, b) => a + availabilityZones[b].length, 0),
    [availabilityZones]
  );

  const maxPrefferedRankSelected = useMemo(
    () =>
      values(availabilityZones ?? {})
        .map((az) => az.map((zone) => zone.preffered))
        .flat()
        .filter((rank) => rank > AZ_NOT_PREFERRED)
        .reduce((maxRank, rank) => Math.max(maxRank, rank), AZ_NOT_PREFERRED),
    [availabilityZones]
  );

  const selectedRegionCount = getEffectiveRegions(
    resilienceAndRegionsSettings,
    placementsByProvider
  ).length;
  const showCrossRegionRankingHint = selectedRegionCount > 1;

  const preferredMenuItems = isPrefferedAllowed
    ? (() => {
        const options = [
          ...(showCrossRegionRankingHint
            ? [
                <StyledRankingHintMenuItem key="ranking-across-regions-hint" disabled>
                  <BulbIcon width={24} height={24} />
                  {t('rankingAcrossRegions')}
                </StyledRankingHintMenuItem>
              ]
            : []),
          <StyledPreferedMenuItem key="no-option-selected" value={AZ_NOT_PREFERRED}>
            <Typography variant="body1">No</Typography>
            <Typography variant="subtitle1">Not Preferred</Typography>
          </StyledPreferedMenuItem>,
          ...Array.from({ length: zonesCount }, (_, i) => i + AZ_PREFFERED_HIGHEST_RANK).map(
            (rank) => (
              <StyledPreferedMenuItem
                key={rank}
                value={rank}
                disabled={rank > maxPrefferedRankSelected + 1}
              >
                <Typography variant="body1">{`Rank ${rank}`}</Typography>
                <Typography variant="subtitle1">
                  {rank === AZ_PREFFERED_HIGHEST_RANK
                    ? 'Default Preferred Zone'
                    : 'Preferred zone if higher-rank zones fail.'}
                </Typography>
              </StyledPreferedMenuItem>
            )
          )
        ];

        return options.flatMap((option, optionIndex) =>
          optionIndex < options.length - 1
            ? [option, <Divider key={`preferred-divider-${optionIndex}`} />]
            : [option]
        );
      })()
    : null;

  const updateNodeCountAcrossRegions = (nodeCount: number) => {
    if (providerUuid) {
      const updatedByProvider: AvailabilityZonesByProvider = {
        ...(availabilityZonesByProvider ?? {})
      };
      for (const [uuid, zonesByRegion] of Object.entries(updatedByProvider)) {
        updatedByProvider[uuid] = Object.fromEntries(
          Object.entries(zonesByRegion ?? {}).map(([code, zones]) => [
            code,
            (zones ?? []).map((zone) =>
              zone.nodeCount !== nodeCount ? { ...zone, nodeCount } : zone
            )
          ])
        );
      }
      setValue(AVAILABILITY_ZONES_BY_PROVIDER, updatedByProvider as any, {
        shouldValidate: false,
        shouldDirty: true
      });
      return;
    }

    const updatedAz = { ...singularAvailabilityZones };
    Object.keys(updatedAz).forEach((regionCode) => {
      updatedAz[regionCode] = updatedAz[regionCode].map((zone) => {
        if (zone.nodeCount !== nodeCount) {
          return { ...zone, nodeCount };
        }
        return zone;
      });
    });

    setValue('availabilityZones', updatedAz, {
      // Avoid validating the entire placement graph on every keypress in guided mode.
      // Post-submit revalidation is already handled by the step-level effect.
      shouldValidate: false,
      shouldDirty: true
    });
  };

  const zone = useWatch({
    name: zonePath as any,
    control
  });
  const isGuidedMode =
    resilienceAndRegionsSettings?.resilienceFormMode === ResilienceFormMode.GUIDED;

  const isNodeInputEditable = !isGuidedMode || (regionIndex === 0 && index === 0);
  const showAzSelectError =
    isSubmitted && (errors as any)?.lesserNodes?.message === 'errMsg.expertAzBlank' && !zone?.name;
  const showOnPremNodeCountError =
    isSubmitted &&
    (providerUuid
      ? (errors as any)?.[AVAILABILITY_ZONES_BY_PROVIDER]?.[providerUuid]?.[region.code]?.[index]
          ?.nodeCount?.message
      : (errors as any)?.availabilityZones?.[region.code]?.[index]?.nodeCount?.message) ===
      'errMsg.onPremFreeNodesExceeded';

  return (
    <div style={{ display: 'flex', gap: '8px', alignItems: 'center', paddingLeft: '8px' }}>
      <span style={{ marginTop: '24px', marginRight: '8px', lineHeight: 0 }}>
        <Return />
      </span>
      <Controller
        control={control}
        name={zonePath as any}
        render={({ field }) => (
          <YBSelect
            label={t('availabilityZone')}
            sx={{ flex: 1, minWidth: 0 }}
            error={showAzSelectError}
            value={zone?.name ?? ''}
            renderValue={(value) =>
              !isGuidedMode && (value === '' || value === null || value === undefined)
                ? tCommon('select')
                : (value as string)
            }
            selectProps={!isGuidedMode ? ({ displayEmpty: true } as any) : undefined}
            onChange={(e) => {
              const selectedZone = region.zones.find((z) => z.name === e.target.value);
              if (selectedZone) {
                field.onChange({
                  ...selectedZone,
                  nodeCount: zone.nodeCount ?? field.value?.nodeCount,
                  preffered: zone.preffered ?? field.value?.preffered ?? AZ_NOT_PREFERRED
                });
              }
            }}
            menuProps={menuProps}
            dataTestId="availability-zone-select"
          >
            {region.zones.map((zoneOption) => (
              <MenuItem
                key={zoneOption.uuid}
                value={zoneOption.name}
                disabled={selectedAzNames.has(zoneOption.name) && zoneOption.name !== field.value?.name}
              >
                {zoneOption.name}
              </MenuItem>
            ))}
          </YBSelect>
        )}
      />
      <Controller
        control={control}
        name={`${zonePath}.nodeCount` as any}
        render={({ field }) => (
          <YBTooltip
            title={
              !isNodeInputEditable && isGuidedMode ? (
                <div>
                  <StyledTooltipText>
                    <Trans t={t} i18nKey="tooltips.guidedNodeCount" components={{ b: <b /> }} />
                  </StyledTooltipText>
                  <StyledTooltipFooter>
                    <BookIcon />
                    <Link underline="always" component="button">
                      {t('tooltips.learnMore')}
                    </Link>
                  </StyledTooltipFooter>
                </div>
              ) : (
                ''
              )
            }
          >
            <span style={{ display: 'inline-block', width: '96px', flexShrink: 0 }}>
              <YBInput
                fullWidth
                type="number"
                label={
                  useDedicatedNodes
                    ? t(isK8s ? 'tServerPods' : 'tServer')
                    : t(isK8s ? 'pods' : 'nodes')
                }
                error={showNodesCountError || showOnPremNodeCountError}
                value={field.value}
                onChange={(e) => {
                  const nextValue = parseInt(e.target.value, 10);
                  if (isNaN(nextValue) || nextValue < 1) {
                    return;
                  }
                  if (
                    resilienceAndRegionsSettings?.resilienceFormMode === ResilienceFormMode.GUIDED
                  ) {
                    updateNodeCountAcrossRegions(nextValue);
                  } else {
                    field.onChange(nextValue);
                  }
                }}
                disabled={!isNodeInputEditable}
                dataTestId="availability-zone-node-count-input"
              />
            </span>
          </YBTooltip>
        )}
      />
      {isPrefferedAllowed && (
        <Controller
          control={control}
          name={`${zonePath}.preffered` as any}
          render={({ field }) => (
            <YBSelect
              label={
                <>
                  <span style={{ lineHeight: '16px' }}>Preferred</span>&nbsp;
                  {index === 0 && (
                    <HelpOutline
                      onClick={() => {
                        setShowPreferredInfoModal(true);
                      }}
                      style={{ cursor: 'pointer' }}
                    />
                  )}
                </>
              }
              sx={{ width: '92px', flexShrink: 0 }}
              value={field.value}
              onChange={(e) => {
                field.onChange(e.target.value);
              }}
              menuProps={menuProps}
              renderValue={(value) => {
                return value === AZ_NOT_PREFERRED ? 'No' : `Rank ${value}`;
              }}
              dataTestId="availability-zone-preferred-select"
            >
              {preferredMenuItems}
            </YBSelect>
          )}
        />
      )}

      {resilienceAndRegionsSettings?.faultToleranceType !== FaultToleranceType.NONE &&
        (selectedAzCountInRegion > 1 ? (
          <IconButton
            aria-label={t('removeAvailabilityZone')}
            onClick={remove}
            data-testid="remove-availability-zone"
            sx={{ marginTop: '24px', marginLeft: '8px' }}
            size="small"
          >
            <RemoveIcon />
          </IconButton>
        ) : (
          <div style={{ marginTop: '24px', marginLeft: '8px', width: '32px', height: '32px' }} />
        ))}
      <PreferredInfoModal
        open={showPreferredInfoModal}
        onClose={() => {
          setShowPreferredInfoModal(false);
        }}
      />
    </div>
  );
};
