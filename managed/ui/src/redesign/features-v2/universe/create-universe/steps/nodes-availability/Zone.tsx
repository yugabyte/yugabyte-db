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

const { MenuItem, Typography, IconButton, Link, styled } = mui;

interface ZoneProps {
  control: Control<NodeAvailabilityProps>;
  index: number;
  region: Region;
  remove: () => void;
  regionIndex?: number;
  setValue: UseFormSetValue<NodeAvailabilityProps>;
  /** When NODE_LEVEL total nodes are below RF (lessNodes / lessNodesDedicated). */
  showNodesCountError?: boolean;
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
  padding: '16px !important',
  gap: '8px',
  height: 'auto',
  justifyContent: 'flex-start',
  alignItems: 'flex-start',
  width: '300px',
  '.MuiTypography-subtitle1': {
    color: theme.palette.grey[700]
  }
}));

export const Zone: FC<ZoneProps> = ({
  control,
  index,
  region,
  remove,
  regionIndex,
  setValue,
  showNodesCountError = false
}) => {
  const {
    formState: { errors, isSubmitted }
  } = useFormContext<NodeAvailabilityProps>();
  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.nodesAndAvailability.availabilityZones'
  });
  const [{ resilienceAndRegionsSettings }] = (useContext(
    CreateUniverseContext
  ) as unknown) as CreateUniverseContextMethods;
  const isPrefferedAllowed = ![FaultToleranceType.NODE_LEVEL, FaultToleranceType.NONE].includes(
    resilienceAndRegionsSettings!.faultToleranceType
  );

  const [showPreferredInfoModal, setShowPreferredInfoModal] = useState(false);

  const availabilityZones = useWatch({ name: 'availabilityZones', control });
  const selectedAzCountInRegion = availabilityZones?.[region.code]?.length ?? 0;
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
        .reduce((a, b) => Math.max(a, b), -1),
    [availabilityZones]
  );

  const preferredMenuItems = isPrefferedAllowed
    ? Array.from({ length: zonesCount }, (_, i) => (
      <StyledPreferedMenuItem key={i} value={i} disabled={i > maxPrefferedRankSelected + 1}>
        <Typography variant="body1">{`Rank ${i + 1}`}</Typography>
        <Typography variant="subtitle1">
          {i === 0 ? 'Default Preferred Zone' : 'Preferred zone if higher-rank zones fail.'}
        </Typography>
      </StyledPreferedMenuItem>
    ))
    : null;

  preferredMenuItems?.unshift(
    <StyledPreferedMenuItem key="no-option-selected" value={-1}>
      <Typography variant="body1">No</Typography>
      <Typography variant="subtitle1">Not Preferred</Typography>
    </StyledPreferedMenuItem>
  );

  const updateNodeCountAcrossRegions = (nodeCount: number) => {
    const updatedAz = { ...availabilityZones };
    Object.keys(updatedAz).forEach((region) => {
      updatedAz[region] = updatedAz[region].map((zone) => {
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
    name: `availabilityZones.${region.code}.${index}`,
    control
  });
  const isGuidedMode = resilienceAndRegionsSettings?.resilienceFormMode === ResilienceFormMode.GUIDED;

  const isNodeInputEditable =
    !isGuidedMode ||
    (regionIndex === 0 && index === 0);
  const showAzSelectError =
    isSubmitted &&
    (errors as any)?.lesserNodes?.message === 'errMsg.expertAzBlank' &&
    !zone?.name;

  return (
    <div style={{ display: 'flex', gap: '8px', alignItems: 'center', padding: '0px 8px' }}>
      <span style={{ marginTop: '24px', lineHeight: 0 }}>
        <Return />
      </span>
      <Controller
        control={control}
        name={`availabilityZones.${region.code}.${index}`}
        render={({ field }) => (
          <YBSelect
            label="Availability Zone"
            sx={{ width: '300px' }}
            error={showAzSelectError}
            value={zone?.name ?? ''}
            renderValue={(value) =>
              !isGuidedMode && (value === '' || value === null || value === undefined)
                ? 'Select'
                : (value as string)
            }
            selectProps={!isGuidedMode ? ({ displayEmpty: true } as any) : undefined}
            onChange={(e) => {
              const selectedZone = region.zones.find((z) => z.name === e.target.value);
              if (selectedZone) {
                field.onChange({ ...field.value, ...selectedZone });
              }
            }}
            menuProps={menuProps}
            dataTestId="availability-zone-select"
          >
            {region.zones.map((zone) => (
              <MenuItem
                key={zone.uuid}
                value={zone.name}
                disabled={selectedAzNames.has(zone.name) && zone.name !== field.value?.name}
              >
                {zone.name}
              </MenuItem>
            ))}
          </YBSelect>
        )}
      />
      <Controller
        control={control}
        name={`availabilityZones.${region.code}.${index}.nodeCount`}
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
            <span style={{ display: 'inline-block' }}>
              <YBInput
                type="number"
                label="Nodes"
                error={showNodesCountError}
                value={field.value}
                onChange={(e) => {
                  const nextValue = parseInt(e.target.value, 10);
                  if (parseInt(e.target.value) < 1) {
                    return;
                  }
                  if (resilienceAndRegionsSettings?.resilienceFormMode === ResilienceFormMode.GUIDED) {
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
          name={`availabilityZones.${region.code}.${index}.preffered`}
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
              sx={{ width: '90px' }}
              value={field.value}
              onChange={(e) => {
                field.onChange(e.target.value);
              }}
              menuProps={menuProps}
              renderValue={(value) => {
                return value === -1 ? 'No' : `Rank ${parseInt(value as string) + 1}`;
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
            aria-label="Remove availability zone"
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
