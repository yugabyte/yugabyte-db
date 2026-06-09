import { ReactElement, useMemo, useEffect, useContext } from 'react';
import { useTranslation } from 'react-i18next';
import { useForm, useFieldArray } from 'react-hook-form';
import { Box, IconButton, Typography, MenuItem, makeStyles } from '@material-ui/core';
import {
  YBButton,
  YBModal,
  YBInput,
  YBLabel,
  YBSelect
} from '../../../../../../components';
import { Placement, AZOverridePerAZ, ClusterType } from '../../../utils/dto';
import { UniverseFormContext } from '../../../UniverseFormContainer';
import DeleteIcon from '../../../../../../assets/delete.svg?img';

export interface AZOverrideRowFormValue {
  azUuid: string;
  azName: string;
  tserver: {
    numVolumes?: number;
    volumeSize?: number;
    storageClass?: string;
  };
  master: {
    numVolumes?: number;
    volumeSize?: number;
    storageClass?: string;
  };
}

export interface AZOverridesFormValue {
  azOverrides: AZOverrideRowFormValue[];
}

function rowToAZOverrideEntry(row: AZOverrideRowFormValue, includeMaster: boolean): AZOverridePerAZ {
  const tserverDeviceInfo: { volumeSize?: number; numVolumes?: number; storageClass?: string } = {};
  if (row.tserver.volumeSize !== undefined && row.tserver.volumeSize !== null)
    tserverDeviceInfo.volumeSize = row.tserver.volumeSize;
  if (row.tserver.numVolumes !== undefined && row.tserver.numVolumes !== null)
    tserverDeviceInfo.numVolumes = row.tserver.numVolumes;
  const tserverSc = row.tserver.storageClass?.trim();
  if (tserverSc) tserverDeviceInfo.storageClass = tserverSc;

  const perProcess: AZOverridePerAZ['perProcess'] = {};
  if (Object.keys(tserverDeviceInfo).length > 0) perProcess.TSERVER = { deviceInfo: tserverDeviceInfo };

  if (includeMaster) {
    const masterDeviceInfo: { volumeSize?: number; numVolumes?: number; storageClass?: string } = {};
    if (row.master.volumeSize !== undefined && row.master.volumeSize !== null)
      masterDeviceInfo.volumeSize = row.master.volumeSize;
    if (row.master.numVolumes !== undefined && row.master.numVolumes !== null)
      masterDeviceInfo.numVolumes = row.master.numVolumes;
    const masterSc = row.master.storageClass?.trim();
    if (masterSc) masterDeviceInfo.storageClass = masterSc;
    if (Object.keys(masterDeviceInfo).length > 0) perProcess.MASTER = { deviceInfo: masterDeviceInfo };
  }

  return Object.keys(perProcess).length > 0 ? { perProcess } : {};
}

/** K8s read replica (async) clusters have no masters; strip legacy MASTER overrides from saved payload. */
function stripMasterFromAzOverrides(overrides: Record<string, AZOverridePerAZ>): void {
  Object.keys(overrides).forEach((uuid) => {
    const entry = overrides[uuid];
    const perProcess = entry?.perProcess;
    if (!perProcess?.MASTER) return;
    const { MASTER: _m, ...rest } = perProcess;
    if (Object.keys(rest).length === 0) delete overrides[uuid];
    else overrides[uuid] = { perProcess: rest };
  });
}

function objectToRow(
  azUuid: string,
  azName: string,
  entry: AZOverridePerAZ
): AZOverrideRowFormValue {
  const t = entry?.perProcess?.TSERVER?.deviceInfo;
  const m = entry?.perProcess?.MASTER?.deviceInfo;
  return {
    azUuid,
    azName,
    tserver: {
      ...(t?.numVolumes !== null && { numVolumes: t?.numVolumes }),
      ...(t?.volumeSize !== null && { volumeSize: t?.volumeSize }),
      ...(t?.storageClass !== null && t?.storageClass !== '' && { storageClass: t?.storageClass as string })
    },
    master: {
      ...(m?.numVolumes !== null && { numVolumes: m?.numVolumes }),
      ...(m?.volumeSize !== null && { volumeSize: m?.volumeSize }),
      ...(m?.storageClass !== null && m?.storageClass !== '' && { storageClass: m?.storageClass as string })
    }
  };
}

const getEmptyRow = (): AZOverrideRowFormValue => ({
  azUuid: '',
  azName: '',
  tserver: {},
  master: {}
});

const useModalStyles = makeStyles((theme) => ({
  azCard: {
    marginBottom: theme.spacing(3),
    padding: theme.spacing(2.5),
    backgroundColor: theme.palette.grey[50],
    borderRadius: theme.spacing(1.5),
    border: `1px solid ${theme.palette.grey[200]}`
  },
  azHeader: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'space-between',
    marginBottom: theme.spacing(2.5),
    paddingBottom: theme.spacing(1.5),
    borderBottom: `1px solid ${theme.palette.grey[200]}`
  },
  azLabel: {
    fontSize: 12,
    fontWeight: 600,
    textTransform: 'uppercase' as const,
    letterSpacing: 0.5,
    color: theme.palette.grey[600]
  },
  removeButton: {
    padding: theme.spacing(0.5),
    color: theme.palette.grey[600],
    '&:hover': {
      backgroundColor: theme.palette.action.hover,
      color: theme.palette.error.main
    }
  },
  processSection: {
    marginTop: theme.spacing(2),
    '&:first-of-type': {
      marginTop: 0
    }
  },
  processLabel: {
    fontSize: 13,
    fontWeight: 600,
    color: theme.palette.grey[800],
    marginBottom: theme.spacing(1.5)
  },
  fieldRow: {
    display: 'flex',
    flexWrap: 'wrap',
    gap: theme.spacing(2),
    alignItems: 'flex-start'
  },
  fieldGroup: {
    minWidth: 100,
    flex: '1 1 120px'
  },
  addButton: {
    marginTop: theme.spacing(1),
    borderStyle: 'dashed',
    borderWidth: 1.5,
    borderColor: theme.palette.grey[400],
    color: theme.palette.grey[700],
    '&:hover': {
      borderColor: theme.palette.primary.main,
      color: theme.palette.primary.main,
      backgroundColor: theme.palette.action.hover
    },
    '&.Mui-disabled': {
      borderColor: theme.palette.grey[300],
      color: theme.palette.grey[500]
    }
  }
}));

interface AZOverridesModalProps {
  open: boolean;
  onClose: () => void;
  onSubmit: (azOverrides: Record<string, AZOverridePerAZ>) => void;
  placements: Placement[];
  initialAzOverrides?: Record<string, AZOverridePerAZ>;
}

export const AZOverridesModal = ({
  open,
  onClose,
  onSubmit,
  placements,
  initialAzOverrides = {}
}: AZOverridesModalProps): ReactElement => {
  const { t } = useTranslation();
  const modalClasses = useModalStyles();
  const { clusterType } = (useContext(UniverseFormContext) as any)[0];
  const tserverOnlyAzOverrides = clusterType === ClusterType.ASYNC;

  const placementUuidList = useMemo(
    () => placements.map((p) => p.uuid).sort().join(','),
    [placements]
  );

  const placementUuidSet = useMemo(
    () => new Set(placements.map((p) => p.uuid)),
    [placementUuidList]
  );

  const initialRows = useMemo(() => {
    const currentPlacementUuids = placementUuidSet;
    const parsed: AZOverrideRowFormValue[] = [];
    const azUuids = Object.keys(initialAzOverrides);
    if (azUuids.length > 0) {
      azUuids.forEach((azUuid) => {
        if (!currentPlacementUuids.has(azUuid)) return;
        const entry = initialAzOverrides[azUuid];
        const placement = placements.find((p) => p.uuid === azUuid);
        const azName = placement?.name ?? azUuid;
        parsed.push(objectToRow(azUuid, azName, entry ?? {}));
      });
    }
    return parsed;
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [open, placementUuidList, placements, JSON.stringify(Object.keys(initialAzOverrides))]);

  const defaultValues: AZOverridesFormValue = {
    azOverrides: initialRows
  };

  const { control, handleSubmit, watch, getValues, setValue, reset } = useForm<AZOverridesFormValue>({
    defaultValues
  });

  useEffect(() => {
    if (open) reset({ azOverrides: initialRows });
  }, [open, initialRows, reset]);

  const { fields, append, remove } = useFieldArray({ control, name: 'azOverrides' });

  const watchedRows = watch('azOverrides') ?? [];
  const usedAzUuids = watchedRows.map((r) => r?.azUuid) ?? [];

  const hasRowWithNoAzSelected = watchedRows.some((r) => !r?.azUuid?.trim());

  const isAzUsedByOtherRow = (azUuid: string, currentIndex: number) =>
    usedAzUuids.some((u, i) => i !== currentIndex && u === azUuid);

  const handleFormSubmit = () => {
    const values = getValues();
    // Preserve entries from other configs (keys that are not placement UUIDs)
    const result: Record<string, AZOverridePerAZ> = { ...initialAzOverrides };
    placementUuidSet.forEach((uuid) => delete result[uuid]);
    values.azOverrides?.forEach((row: AZOverrideRowFormValue) => {
      if (row?.azUuid) {
        const entry = rowToAZOverrideEntry(row, !tserverOnlyAzOverrides);
        if (Object.keys(entry).length > 0) result[row.azUuid] = entry;
      }
    });
    if (tserverOnlyAzOverrides) stripMasterFromAzOverrides(result);
    onSubmit(result);
    onClose();
  };

  const addAZ = () => {
    append(getEmptyRow());
  };

  return (
    <YBModal
      open={open}
      size="lg"
      overrideHeight="fit-content"
      title={t('universeForm.azOverrides.modalTitle')}
      cancelLabel={t('common.cancel')}
      submitLabel={t('common.save')}
      cancelTestId="AZOverridesModal-CancelButton"
      submitTestId="AZOverridesModal-SubmitButton"
      onClose={onClose}
      titleSeparator
      onSubmit={handleFormSubmit}
      buttonProps={{ primary: { disabled: hasRowWithNoAzSelected } }}
      dialogContentProps={{ dividers: true }}
      scroll='body'
    >
      <form onSubmit={handleSubmit(handleFormSubmit)}>
        {fields.length > 0 &&
          fields.map((field, index) => (
            <Box key={field.id} className={modalClasses.azCard}>
              <Box className={modalClasses.azHeader}>
                <Box display="flex" alignItems="center" width="50%">
                  <Typography className={modalClasses.azLabel}>
                    {t('universeForm.azOverrides.availabilityZone')}
                  </Typography>
                  <Box display="flex" alignItems="center" flex={1} ml={2}>
                    <YBSelect
                      value={watch(`azOverrides.${index}.azUuid`) ?? ''}
                      onChange={(e) => {
                        const uuid = (e.target.value as string) || '';
                        const pl = uuid ? placements.find((p) => p.uuid === uuid) : null;
                        setValue(`azOverrides.${index}.azUuid`, uuid);
                        setValue(`azOverrides.${index}.azName`, pl?.name ?? '');
                      }}
                      fullWidth
                      inputProps={{ 'data-testid': `AZOverridesModal-AZSelect-${index}` }}
                      renderValue={(selected) => {
                        if (!selected) return null;
                        const pl = placements.find((p) => p.uuid === selected);
                        return pl?.name ?? selected;
                      }}
                    >
                      {placements.map((p) => (
                        <MenuItem
                          key={p.uuid}
                          value={p.uuid}
                          disabled={isAzUsedByOtherRow(p.uuid, index)}
                        >
                          {p.name}
                        </MenuItem>
                      ))}
                    </YBSelect>
                  </Box>
                </Box>
                <IconButton
                  size="small"
                  className={modalClasses.removeButton}
                  onClick={() => remove(index)}
                  data-testid={`AZOverridesModal-Remove-${index}`}
                  aria-label={t('common.remove')}
                >
                  <img src={DeleteIcon} alt="" width={16} height={16} />
                </IconButton>
              </Box>

              <Box className={modalClasses.processSection}>
                <Typography className={modalClasses.processLabel}>
                  {t('universeForm.tserver')}
                </Typography>
                <Box className={modalClasses.fieldRow}>
                  <Box className={modalClasses.fieldGroup}>
                    <YBLabel>{t('universeForm.azOverrides.volumeCount')}</YBLabel>
                    <YBInput
                      type="number"
                      fullWidth
                      value={watch(`azOverrides.${index}.tserver.numVolumes`) ?? ''}
                      onChange={(e) => {
                        const v = e.target.value;
                        setValue(
                          `azOverrides.${index}.tserver.numVolumes`,
                          v === '' ? undefined : Number(v) || undefined
                        );
                      }}
                      inputProps={{ min: 1 }}
                    />
                  </Box>
                  <Box className={modalClasses.fieldGroup}>
                    <YBLabel>{t('universeForm.azOverrides.volumeSizeGib')}</YBLabel>
                    <YBInput
                      type="number"
                      fullWidth
                      value={watch(`azOverrides.${index}.tserver.volumeSize`) ?? ''}
                      onChange={(e) => {
                        const v = e.target.value;
                        setValue(
                          `azOverrides.${index}.tserver.volumeSize`,
                          v === '' ? undefined : Number(v) || undefined
                        );
                      }}
                      inputProps={{ min: 1 }}
                    />
                  </Box>
                  <Box className={modalClasses.fieldGroup}>
                    <YBLabel>{t('universeForm.instanceConfig.storageClass')}</YBLabel>
                    <YBInput
                      fullWidth
                      value={watch(`azOverrides.${index}.tserver.storageClass`) ?? ''}
                      onChange={(e) =>
                        setValue(`azOverrides.${index}.tserver.storageClass`, e.target.value || undefined)
                      }
                    />
                  </Box>
                </Box>
              </Box>

              {!tserverOnlyAzOverrides && (
                <Box className={modalClasses.processSection}>
                  <Typography className={modalClasses.processLabel}>
                    {t('universeForm.master')}
                  </Typography>
                  <Box className={modalClasses.fieldRow}>
                    <Box className={modalClasses.fieldGroup}>
                      <YBLabel>{t('universeForm.azOverrides.volumeCount')}</YBLabel>
                      <YBInput
                        type="number"
                        fullWidth
                        value={watch(`azOverrides.${index}.master.numVolumes`) ?? ''}
                        onChange={(e) => {
                          const v = e.target.value;
                          setValue(
                            `azOverrides.${index}.master.numVolumes`,
                            v === '' ? undefined : Number(v) || undefined
                          );
                        }}
                        inputProps={{ min: 1 }}
                      />
                    </Box>
                    <Box className={modalClasses.fieldGroup}>
                      <YBLabel>{t('universeForm.azOverrides.volumeSizeGib')}</YBLabel>
                      <YBInput
                        type="number"
                        fullWidth
                        value={watch(`azOverrides.${index}.master.volumeSize`) ?? ''}
                        onChange={(e) => {
                          const v = e.target.value;
                          setValue(
                            `azOverrides.${index}.master.volumeSize`,
                            v === '' ? undefined : Number(v) || undefined
                          );
                        }}
                        inputProps={{ min: 1 }}
                      />
                    </Box>
                    <Box className={modalClasses.fieldGroup}>
                      <YBLabel>{t('universeForm.instanceConfig.storageClass')}</YBLabel>
                      <YBInput
                        fullWidth
                        value={watch(`azOverrides.${index}.master.storageClass`) ?? ''}
                        onChange={(e) =>
                          setValue(`azOverrides.${index}.master.storageClass`, e.target.value || undefined)
                        }
                      />
                    </Box>
                  </Box>
                </Box>
              )}
            </Box>
          ))}
        <YBButton
          variant="primary"
          data-testid="AZOverridesModal-AddAZ"
          onClick={addAZ}
          disabled={placements.length === 0 || fields.length >= placements.length}
        >
          <span className="fa fa-plus" />
          {fields.length === 0
            ? t('universeForm.azOverrides.addAZ')
            : t('universeForm.azOverrides.addAnotherAZ')}
        </YBButton>
      </form>
    </YBModal>
  );
};
