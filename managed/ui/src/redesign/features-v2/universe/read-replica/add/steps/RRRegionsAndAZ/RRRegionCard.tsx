import { FC, useMemo } from 'react';
import { Controller, useFieldArray, useFormContext, useWatch } from 'react-hook-form';
import _ from 'lodash';
import { Trans, useTranslation } from 'react-i18next';
import { YBButton, YBInput, YBSelect, YBTag, YBTooltip, mui } from '@yugabyte-ui-library/core';
import { Region } from '@app/redesign/features/universe/universe-form/utils/dto';
import Return from '@app/redesign/assets/tree.svg';
import AddIcon from '@app/redesign/assets/add2.svg';
import RemoveAzIcon from '@app/redesign/assets/close-large.svg';
import DeleteOutlineIcon from '@app/redesign/assets/delete2.svg';
import {
  DEFAULT_READ_REPLICA_NODE_COUNT,
  DEFAULT_READ_REPLICA_RF,
  getEmptyRRPlacementZone,
  RRPlacementRegionForm,
  RRPlacementZoneForm,
  RRRegionsAndAZFormValues
} from './dtos';
import BookIcon from '@app/redesign/assets/blue-book.svg';
import { getFlagFromRegion } from '../../../../create-universe/helpers/RegionToFlagUtils';
import InfoIcon from '@app/redesign/assets/info-message.svg';

const { Box, MenuItem, Typography, styled, IconButton } = mui;

const REGION_LABEL_COL_WIDTH = 72;

function zoneSnapshot(z: RRPlacementZoneForm | undefined) {
  if (!z) return null;
  return {
    zoneUuid: z.zoneUuid,
    nodeCount: z.nodeCount,
    dataCopies: z.dataCopies
  };
}

function firstUnusedZoneUuid(
  zonesInRegion: { uuid: string }[],
  usedUuids: Array<string | null | undefined>
): string | null {
  const used = new Set(usedUuids.filter((u): u is string => Boolean(u)));
  return zonesInRegion.find((z) => !used.has(z.uuid))?.uuid ?? null;
}

const menuProps = {
  anchorOrigin: { vertical: 'bottom', horizontal: 'left' },
  transformOrigin: { vertical: 'top', horizontal: 'left' }
} as const;

const RegionSelectValue = ({ region }: { region: Region }) => {
  const flag = getFlagFromRegion(region.code);
  return (
    <Box component="span" sx={{ display: 'flex', alignItems: 'center', gap: 1, minWidth: 0 }}>
      {flag ? (
        <Box
          component="span"
          sx={{ fontSize: '16px', lineHeight: 1, flexShrink: 0 }}
          aria-hidden
        >
          {flag}
        </Box>
      ) : null}
      <Typography
        component="span"
        noWrap
        sx={{ fontSize: '13px', fontWeight: 400, lineHeight: '16px', color: 'grey.900' }}
      >
        {region.name} ({region.code})
      </Typography>
    </Box>
  );
};

const StyledInnerCard = styled('div')(({ theme }) => ({
  background: '#FBFCFD',
  display: 'flex',
  flexDirection: 'column',
  gap: theme.spacing(3),
  border: `1px solid ${theme.palette.grey[300]}`,
  borderRadius: '8px',
  width: '100%',
  paddingTop: theme.spacing(1)
}));

const StyledTooltipText = styled('div')(({ theme }) => ({
  fontSize: '11.5px',
  lineHeight: '16px',
  fontWeight: 400,
  color: theme.palette.grey[700],
  maxWidth: 280
}));

const StyledRegionHeader = styled('div')(({ theme }) => ({
  display: 'flex',
  flexDirection: 'row',
  justifyContent: 'space-between',
  alignItems: 'center',
  padding: theme.spacing(1.25, 3),
  gap: theme.spacing(2)
}));

type Props = {
  regionIndex: number;
  regionsList: Region[];
  /** Baseline row at this index (for Undo); undefined for wizard-added rows past initial snapshot. */
  baselineRegion?: RRPlacementRegionForm;
  /** When false, per-AZ Undo is hidden (e.g. default placement, not loaded from existing read replica). */
  allowAzUndo?: boolean;
  showRemoveRegion?: boolean;
  onRemoveRegion?: () => void;
  /** When primary uses dedicated nodes, AZ node inputs are labeled as T-Server counts. */
  useDedicatedNodes?: boolean;
  isK8s?: boolean;
};

export const RRRegionCard: FC<Props> = ({
  regionIndex,
  regionsList,
  baselineRegion,
  allowAzUndo = false,
  showRemoveRegion,
  onRemoveRegion,
  isK8s = false
}) => {
  const { control, watch, setValue, getValues } = useFormContext<RRRegionsAndAZFormValues>();
  const { t } = useTranslation('translation', { keyPrefix: 'readReplica.addRR' });
  const { t: tc } = useTranslation('translation', { keyPrefix: 'common' });

  const { fields, append, remove } = useFieldArray({
    control,
    name: `regions.${regionIndex}.zones`
  });

  const regionRow = useWatch({ control, name: `regions.${regionIndex}` }) as
    | RRPlacementRegionForm
    | undefined;
  const isNewRegion = Boolean(regionRow?.isNew);
  const baselineZoneUuids = useMemo(() => {
    const uuids = (baselineRegion?.zones ?? [])
      .map((z) => z.zoneUuid)
      .filter((u): u is string => Boolean(u));
    return new Set(uuids);
  }, [baselineRegion]);
  // Only compare against a real loaded RR placement (edit flow). Add-RR defaults have null uuids.
  const isNewAz = (zone: RRPlacementZoneForm | undefined) => {
    if (isNewRegion || baselineZoneUuids.size === 0) return false;
    if (!zone?.zoneUuid) return true;
    return !baselineZoneUuids.has(zone.zoneUuid);
  };

  
  const allRegionRows = useWatch({ control, name: 'regions' }) ?? [];
  const selectedRegionUuidsKey = allRegionRows.map((row) => row?.regionUuid ?? '').join(',');
  const regionUuidsSelectedElsewhere = useMemo(() => {
    const s = new Set<string>();
    allRegionRows.forEach((row, idx) => {
      if (idx !== regionIndex && row?.regionUuid) {
        s.add(row.regionUuid);
      }
    });
    return s;
  }, [allRegionRows, regionIndex, selectedRegionUuidsKey]);

  const regionUuid = watch(`regions.${regionIndex}.regionUuid`);
  const selectedRegion = useMemo(
    () => regionsList.find((r) => r.uuid === regionUuid),
    [regionsList, regionUuid]
  );
  const zonesInRegion = selectedRegion?.zones ?? [];

  const watchedZones = watch(`regions.${regionIndex}.zones`) ?? [];

  const onRegionUuidChange = (uuid: string | null) => {
    setValue(`regions.${regionIndex}.regionUuid`, uuid, { shouldValidate: true, shouldDirty: true });
    const region = regionsList.find((r) => r.uuid === uuid);
    const firstAzUuid = region?.zones?.[0]?.uuid ?? null;
    setValue(
      `regions.${regionIndex}.zones`,
      [
        {
          ...getEmptyRRPlacementZone({
            nodeCount: DEFAULT_READ_REPLICA_NODE_COUNT,
            dataCopies: DEFAULT_READ_REPLICA_RF
          }),
          zoneUuid: firstAzUuid
        }
      ],
      { shouldValidate: true, shouldDirty: true }
    );
  };

  const onUndoAz = (zoneIndex: number) => {
    const bz = baselineRegion?.zones?.[zoneIndex];
    if (!bz) return;
    setValue(`regions.${regionIndex}.zones.${zoneIndex}`, _.cloneDeep(bz), {
      shouldValidate: true,
      shouldDirty: true
    });
  };

  const canAddAnotherAz = Boolean(selectedRegion && fields.length < zonesInRegion.length);
  const selectRegionFirstTooltip = !selectedRegion ? t('firstSelectARegion') : '';

  return (
    <StyledInnerCard>
      {isNewRegion ? (
        <Box
          sx={{ px: 3, pt: 1.25, pb: 0, mb: -1 }}
          data-testid={`rr-region-new-badge-${regionIndex}`}
        >
          <YBTag size="small" variant="light" color="gradient">
            {t('newRegionBadge')}
          </YBTag>
        </Box>
      ) : null}
      <StyledRegionHeader>
        <Box
          sx={{
            display: 'flex',
            flexDirection: 'row',
            alignItems: 'center',
            gap: 3,
            flex: 1,
            minWidth: 0
          }}
        >
          <Typography
            color="textSecondary"
            variant="body1"
            sx={{
              fontSize: '13px',
              fontWeight: 600,
              width: REGION_LABEL_COL_WIDTH,
              flexShrink: 0
            }}
          >
            {t('regionLabel', { index: regionIndex + 1 })}
          </Typography>
          <Box sx={{ flex: 1, minWidth: 0 }}>
            <Controller
              control={control}
              name={`regions.${regionIndex}.regionUuid`}
              render={({ field, fieldState }) => (
                <YBSelect
                  label=""
                  error={Boolean(fieldState.error)}
                  value={field.value ?? ''}
                  renderValue={(v) => {
                    const val = v as string;
                    if (!val) return tc('select');
                    const r = regionsList.find((x) => x.uuid === val);
                    return r ? <RegionSelectValue region={r} /> : tc('select');
                  }}
                  selectProps={{ displayEmpty: true } as any}
                  onChange={(e) => {
                    const v = e.target.value as string;
                    onRegionUuidChange(v || null);
                  }}
                  menuProps={menuProps as any}
                  dataTestId={`rr-region-select-${regionIndex}`}
                  sx={{ width: '100%' }}
                >
                  {regionsList.map((r) => (
                    <MenuItem
                      key={r.uuid}
                      value={r.uuid}
                      disabled={
                        regionUuidsSelectedElsewhere.has(r.uuid) && r.uuid !== (field.value ?? '')
                      }
                    >
                      <RegionSelectValue region={r} />
                    </MenuItem>
                  ))}
                </YBSelect>
              )}
            />
          </Box>
        </Box>
        {showRemoveRegion && onRemoveRegion ? (
          <IconButton
            size="small"
            onClick={onRemoveRegion}
            aria-label={t('removeRegion')}
            data-testid={`rr-remove-region-${regionIndex}`}
            sx={{ color: 'grey.600', flexShrink: 0 }}
          >
            <DeleteOutlineIcon />
          </IconButton>
        ) : null}
      </StyledRegionHeader>

      <Box
        sx={{
          display: 'flex',
          flexDirection: 'column',
          gap: '24px',
          px: 3,
          pb: 3
        }}
      >
        {fields.map((fieldItem, zoneIndex) => {
          const baselineZone = baselineRegion?.zones?.[zoneIndex];
          const currentZone = regionRow?.zones?.[zoneIndex];
          const azDirtyVsBaseline =
            allowAzUndo &&
            !isNewRegion &&
            baselineZone !== undefined &&
            baselineZone !== null &&
            currentZone !== undefined &&
            currentZone !== null &&
            !_.isEqual(zoneSnapshot(currentZone), zoneSnapshot(baselineZone));

          return (
            <Box key={fieldItem.id} sx={{ display: 'flex', flexDirection: 'column', gap: 1 }}>
              {isNewAz(fieldItem) ? (
                <Box
                  sx={{ pl: `${REGION_LABEL_COL_WIDTH + 24}px` }}
                  data-testid={`rr-region-new-az-badge-${regionIndex}-${zoneIndex}`}
                >
                  <YBTag size="small" variant="light" color="gradient">
                    {t('newRegionBadge')}
                  </YBTag>
                </Box>
              ) : null}
              <Box sx={{ display: 'flex', gap: 3, alignItems: 'flex-start', width: '100%' }}>
                <Box
                  sx={{
                    width: REGION_LABEL_COL_WIDTH,
                    pt: '32px',
                    flexShrink: 0,
                    display: 'flex',
                    justifyContent: 'flex-end'
                  }}
                >
                  <Return />
                </Box>
                <Box
                  sx={{ display: 'flex', flex: 1, gap: '8px', alignItems: 'flex-end', minWidth: 0 }}
                >
                  <Box sx={{ flex: '1 1 200px', minWidth: 0 }}>
                    <Controller
                      control={control}
                      name={`regions.${regionIndex}.zones.${zoneIndex}.zoneUuid`}
                      render={({ field, fieldState }) => (
                        <YBTooltip title={selectRegionFirstTooltip}>
                          <span style={{ display: 'block', width: '100%' }}>
                            <YBSelect
                              label={t('availabilityZone')}
                              error={Boolean(fieldState.error)}
                              value={field.value ?? ''}
                              renderValue={(v) => {
                                const val = v as string;
                                if (!val) return tc('select');
                                const z = zonesInRegion.find((x) => x.uuid === val);
                                return z?.name ?? tc('select');
                              }}
                              selectProps={{ displayEmpty: true } as any}
                              disabled={!selectedRegion}
                              onChange={(e) => {
                                const v = e.target.value as string;
                                field.onChange(v || null);
                              }}
                              menuProps={menuProps as any}
                              dataTestId={`rr-az-select-${regionIndex}-${zoneIndex}`}
                              sx={{
                                width: '100%',
                                '& .MuiOutlinedInput-root': {
                                  backgroundColor: !field.value ? 'action.hover' : undefined
                                }
                              }}
                            >
                              {zonesInRegion.map((z) => (
                                <MenuItem
                                  key={z.uuid}
                                  value={z.uuid}
                                  disabled={watchedZones.some(
                                    (row, idx) =>
                                      idx !== zoneIndex &&
                                      Boolean(row?.zoneUuid) &&
                                      row.zoneUuid === z.uuid
                                  )}
                                >
                                  {z.name}
                                </MenuItem>
                              ))}
                            </YBSelect>
                          </span>
                        </YBTooltip>
                      )}
                    />
                  </Box>
                  <Box sx={{ width: '128px', flexShrink: 0 }}>
                    <Box
                      sx={{
                        display: 'flex',
                        alignItems: 'center',
                        gap: 0.5,
                        mb: '4px',
                        minHeight: 20
                      }}
                    >
                      <Typography
                        component="span"
                        noWrap
                        sx={{
                          fontSize: '13px',
                          fontWeight: 500,
                          color: 'grey.600',
                          lineHeight: '16px',
                          minWidth: 0
                        }}
                      >
                        {t('replicationFactor')}
                      </Typography>
                      {zoneIndex === 0 ? (
                        <YBTooltip
                          title={
                            <StyledTooltipText>
                              <Trans
                                t={t}
                                i18nKey="replicationFactorTooltip"
                                components={{
                                  br: <br />,
                                  a: (
                                    <a
                                      href="#"
                                      rel="noopener noreferrer"
                                      onClick={(e) => e.stopPropagation()}
                                      style={{
                                        textDecoration: 'underline',
                                        fontSize: '11.5px',
                                        lineHeight: '16px'
                                      }}
                                    />
                                  )
                                }}
                              />
                              <Box
                                sx={{
                                  display: 'flex',
                                  gap: '4px',
                                  alignItems: 'center',
                                  marginTop: '8px'
                                }}
                              >
                                <BookIcon />
                                <a
                                  style={{
                                    color: '#2B59C3',
                                    textDecoration: 'underline',
                                    fontSize: '11.5px',
                                    lineHeight: '16px'
                                  }}
                                  href="#"
                                  rel="noopener noreferrer"
                                  onClick={(e) => e.stopPropagation()}
                                >
                                  {t('learnMore', { keyPrefix: 'common' })}
                                </a>
                              </Box>
                            </StyledTooltipText>
                          }
                        >
                          <Box
                            component="span"
                            data-testid={`rr-replication-factor-help-${regionIndex}-${zoneIndex}`}
                            sx={{
                              display: 'inline-flex',
                              alignItems: 'center',
                              flexShrink: 0
                            }}
                          >
                            <InfoIcon fontSize="small" />
                          </Box>
                        </YBTooltip>
                      ) : null}
                    </Box>
                    <Controller
                      control={control}
                      name={`regions.${regionIndex}.zones.${zoneIndex}.dataCopies`}
                      render={({ field, fieldState }) => {
                        const ncPath =
                          `regions.${regionIndex}.zones.${zoneIndex}.nodeCount` as const;
                        return (
                          <YBInput
                            label=""
                            type="number"
                            error={Boolean(fieldState.error)}
                            value={field.value}
                            onChange={(e) => {
                              const dc = parseInt(e.target.value, 10);
                              if (Number.isNaN(dc) || dc < 1) return;
                              const clamped = Math.max(1, Math.min(200, Math.floor(dc)));
                              field.onChange(clamped);
                              const nodeCount = Number(getValues(ncPath));
                              if (!Number.isFinite(nodeCount) || nodeCount < clamped) {
                                setValue(ncPath, clamped, {
                                  shouldDirty: true,
                                  shouldValidate: true
                                });
                              }
                            }}
                            dataTestId={`rr-replication-factor-${regionIndex}-${zoneIndex}`}
                          />
                        );
                      }}
                    />
                  </Box>
                  <Box sx={{ width: '96px', flexShrink: 0 }}>
                    <Typography
                      component="div"
                      sx={{
                        fontSize: '13px',
                        fontWeight: 500,
                        color: 'grey.600',
                        lineHeight: '16px',
                        mb: '4px',
                        minHeight: 20
                      }}
                    >
                      {t(isK8s ? 'pods' : 'nodes')}
                    </Typography>
                    <Controller
                      control={control}
                      name={`regions.${regionIndex}.zones.${zoneIndex}.nodeCount`}
                      render={({ field, fieldState }) => {
                        const dcPath =
                          `regions.${regionIndex}.zones.${zoneIndex}.dataCopies` as const;
                        return (
                          <YBInput
                            label=""
                            type="number"
                            error={Boolean(fieldState.error)}
                            value={field.value}
                            onChange={(e) => {
                              const parsed = parseInt(e.target.value, 10);
                              if (Number.isNaN(parsed) || parsed < 1) return;
                              const n = Math.max(1, Math.min(200, Math.floor(parsed)));
                              field.onChange(n);
                              const dc = Number(getValues(dcPath));
                              if (Number.isFinite(dc) && dc > n) {
                                setValue(dcPath, n, {
                                  shouldDirty: true,
                                  shouldValidate: true
                                });
                              }
                            }}
                            dataTestId={`rr-node-count-${regionIndex}-${zoneIndex}`}
                          />
                        );
                      }}
                    />
                  </Box>
                </Box>
                {fields.length > 1 ? (
                  <IconButton
                    size="small"
                    onClick={() => remove(zoneIndex)}
                    data-testid={`rr-remove-az-${regionIndex}-${zoneIndex}`}
                    sx={{ marginTop: '24px', flexShrink: 0 }}
                  >
                    <RemoveAzIcon />
                  </IconButton>
                ) : (
                  <Box sx={{ marginTop: '24px', width: 32, height: 32, flexShrink: 0 }} aria-hidden />
                )}
              </Box>
              {azDirtyVsBaseline ? (
                <Box sx={{ display: 'flex', justifyContent: 'flex-end', pt: 0.5, pr: 3 }}>
                  <YBButton
                    dataTestId={`rr-undo-az-button-${regionIndex}-${zoneIndex}`}
                    variant="ghost"
                    size="small"
                    onClick={() => onUndoAz(zoneIndex)}
                  >
                    {t('undoChange')}
                  </YBButton>
                </Box>
              ) : null}
            </Box>
          );
        })}

        <Box sx={{ display: 'flex', gap: 3 }}>
          <Box sx={{ width: REGION_LABEL_COL_WIDTH, flexShrink: 0 }} />
          <YBTooltip title={selectRegionFirstTooltip}>
            <span style={{ width: 'fit-content' }}>
              <YBButton
                variant="secondary"
                size="medium"
                disabled={!canAddAnotherAz}
                startIcon={<AddIcon />}
                onClick={() => {
                  const nextZoneUuid = firstUnusedZoneUuid(
                    zonesInRegion,
                    watchedZones.map((z) => z.zoneUuid)
                  );
                  append({
                    ...getEmptyRRPlacementZone({
                      nodeCount: DEFAULT_READ_REPLICA_NODE_COUNT,
                      dataCopies: DEFAULT_READ_REPLICA_RF
                    }),
                    zoneUuid: nextZoneUuid
                  });
                }}
                dataTestId={`rr-add-az-${regionIndex}`}
              >
                {t('addAvailabilityZone')}
              </YBButton>
            </span>
          </YBTooltip>
        </Box>
      </Box>
    </StyledInnerCard>
  );
};
