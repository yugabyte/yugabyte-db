import { ReactElement, useEffect, useMemo } from 'react';
import { mui, yba, YBButton, YBInput, YBSelect } from '@yugabyte-ui-library/core';
import { useTranslation } from 'react-i18next';
import { useForm, useFieldArray } from 'react-hook-form';
import { useYBToast } from '../../create-universe/helpers/ToastUtils';
import { useEditUniverse } from '@app/v2/api/universe/universe';
import { ClusterSpec, ClusterSpecClusterType } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { createErrorMessage } from '@app/utils/ObjectUtils';
import TrashIcon from '@app/redesign/assets/delete2.svg';
import AddCircleIcon from '@app/redesign/assets/add-circle-blue.svg';
import { getClusterByType, useEditUniverseContext } from '../EditUniverseUtils';
import { useEditUniverseTaskHandler } from '../hooks/useEditUniverseTaskHandler';
import {
  AzOption,
  buildNodeSpecWithAzOverrides,
  getAzListFromCluster,
  getEmptyStorageOverrideRow,
  rowsToAzNodeSpec,
  StorageOverrideRowFormValue,
  StorageOverridesFormValue,
  azNodeSpecToRows
} from './storageOverridesUtils';

import NextLineIcon from '@app/redesign/assets/tree-icon.svg';

const { YBModal } = yba;
const { Box, MenuItem, Typography, styled } = mui;

const ModalForm = styled(Box)({
  display: 'flex',
  flexDirection: 'column',
  gap: '16px'
});

const AzCard = styled(Box)({
  display: 'flex',
  flexDirection: 'column',
  gap: '24px',
  padding: '24px',
  borderRadius: '8px',
  border: '1px solid #D7DEE4',
  backgroundColor: '#FBFCFD'
});

const AzHeader = styled(Box)({
  display: 'flex',
  alignItems: 'flex-start',
  justifyContent: 'space-between',
  width: '100%'
});

const AzSelectColumn = styled(Box)({
  display: 'flex',
  flexDirection: 'column',
  gap: '2px',
  width: '280px'
});

const ServerConfigPanel = styled(Box)({
  padding: '16px',
  borderRadius: '8px',
  border: '1px solid #D7DEE4',
  backgroundColor: '#F7F9FB',
  display: 'flex',
  flexDirection: 'column',
  gap: '16px',
  width: '100%'
});

const ProcessBlock = styled(Box)({
  display: 'flex',
  flexDirection: 'column',
  gap: '16px',
  width: '100%'
});

const ProcessFieldsRow = styled(Box)({
  display: 'flex',
  alignItems: 'flex-start',
  gap: '16px',
  width: '100%'
});

const TreeIconWrapper = styled(Box)({
  display: 'flex',
  alignItems: 'center',
  paddingTop: '32px'
});

const FieldsContainer = styled(Box)({
  display: 'flex',
  gap: '16px',
  alignItems: 'flex-end'
});

const StorageClassField = styled(Box)({
  width: '208px'
});

interface StorageOverridesModalProps {
  cluster: ClusterSpec;
  open: boolean;
  onClose: () => void;
  azList: AzOption[];
  initialRows: StorageOverrideRowFormValue[];
  hasReadReplica?: boolean;
}

interface ProcessFieldsProps {
  index: number;
  process: 'tserver' | 'master';
  label: string;
  watch: ReturnType<typeof useForm<StorageOverridesFormValue>>['watch'];
  setValue: ReturnType<typeof useForm<StorageOverridesFormValue>>['setValue'];
  t: (key: string) => string;
}

const ProcessStorageFields = ({
  index,
  process,
  label,
  watch,
  setValue,
  t
}: ProcessFieldsProps): ReactElement => {
  const basePath = `overrides.${index}.${process}` as const;

  return (
    <ProcessBlock>
      <Typography
        variant="body2"
        fontWeight={600}
        fontSize="13px"
        color="#4E5F6D"
        lineHeight="16px"
      >
        {label}
      </Typography>
      <ProcessFieldsRow>
        <TreeIconWrapper>
          <NextLineIcon style={{ width: '24px', height: '24px' }} />
        </TreeIconWrapper>
        <FieldsContainer>
          <Box>
            <YBInput
              type="number"
              label={t('volumeCount')}
              sx={{ width: '115px' }}
              value={watch(`${basePath}.numVolumes`) ?? ''}
              onChange={(e) => {
                const v = e.target.value;
                setValue(
                  `${basePath}.numVolumes`,
                  v === '' ? undefined : Number(v) || undefined
                );
              }}
              slotProps={{ htmlInput: { min: 1 } }}
              dataTestId={`StorageOverridesModal-${process}-VolumeCount-${index}`}
            />
          </Box>
          <Box>
            <YBInput
              type="number"
              label={t('volumeSizeGib')}
              sx={{ width: '115px' }}
              value={watch(`${basePath}.volumeSize`) ?? ''}
              onChange={(e) => {
                const v = e.target.value;
                setValue(
                  `${basePath}.volumeSize`,
                  v === '' ? undefined : Number(v) || undefined
                );
              }}
              slotProps={{ htmlInput: { min: 1 } }}
              dataTestId={`StorageOverridesModal-${process}-VolumeSize-${index}`}
            />
          </Box>
          <StorageClassField>
            <YBInput
              label={t('storageClass')}
              sx={{ width: '208px' }}
              value={watch(`${basePath}.storageClass`) ?? ''}
              onChange={(e) =>
                setValue(`${basePath}.storageClass`, e.target.value || undefined)
              }
              dataTestId={`StorageOverridesModal-${process}-StorageClass-${index}`}
            />
          </StorageClassField>
        </FieldsContainer>
      </ProcessFieldsRow>
    </ProcessBlock>
  );
};

export const StorageOverridesModal = ({
  open,
  onClose,
  azList,
  initialRows,
  cluster,
  hasReadReplica = false
}: StorageOverridesModalProps): ReactElement => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'editUniverse.hardware.storageOverrides'
  });
  const { universeData } = useEditUniverseContext();
  const editUniverse = useEditUniverse();
  const universeUUID = universeData?.info?.universe_uuid;
  const handleEditUniverseSuccess = useEditUniverseTaskHandler(universeUUID);

  // Read replica clusters have TServer nodes only — no Master process.
  const isReadReplica = cluster.cluster_type === ClusterSpecClusterType.ASYNC;
  const modalTitle = !hasReadReplica
    ? t('modalTitle')
    : isReadReplica
      ? t('asyncClusterTitle')
      : t('primaryClusterTitle');

  const defaultValues: StorageOverridesFormValue = useMemo(
    () => ({
      overrides: initialRows.length > 0 ? initialRows : [getEmptyStorageOverrideRow()]
    }),
    [initialRows]
  );
  const { control, watch, getValues, setValue } =
    useForm<StorageOverridesFormValue>({
      defaultValues
    });

  const { fields, append, remove, replace } = useFieldArray({ control, name: 'overrides' });

  useEffect(() => {
    if (open) {
      replace(initialRows.length > 0 ? initialRows : [getEmptyStorageOverrideRow()]);
    }
  }, [open, initialRows, replace]);

  const watchedRows = watch('overrides') ?? [];
  const usedAzUuids = watchedRows.map((row) => row?.azUuid) ?? [];
  const hasRowWithNoAzSelected = watchedRows.some((row) => !row?.azUuid?.trim());
  const toast = useYBToast();

  const isAzUsedByOtherRow = (azUuid: string, currentIndex: number) =>
    usedAzUuids.some((uuid, index) => index !== currentIndex && uuid === azUuid);

  const handleFormSubmit = () => {
    if (!universeUUID || !cluster?.uuid || !cluster.node_spec) {
      toast.success(t('unableToApplyChanges', { keyPrefix: 'editUniverse.hardware' }));
      return;
    }

    const values = getValues();
    const azNodeSpec = rowsToAzNodeSpec(values.overrides ?? [], {
      includeMaster: !isReadReplica
    });
    const nodeSpec = buildNodeSpecWithAzOverrides(cluster.node_spec, azNodeSpec);

    editUniverse.mutate(
      {
        uniUUID: universeUUID,
        data: {
          expected_universe_version: -1,
          clusters: [
            {
              uuid: cluster.uuid,
              node_spec: nodeSpec
            }
          ]
        }
      },
      {
        onSuccess: (response: { task_uuid?: string }) => {
          handleEditUniverseSuccess(response.task_uuid);
          toast.success(t('applyingChanges'));
          onClose();
        },
        onError: (error: unknown) => {
          toast.error(createErrorMessage(error));
        }
      }
    );
  };

  const addAvailabilityZone = () => {
    append(getEmptyStorageOverrideRow());
  };

  return (
    <YBModal
      open={open}
      size="lg"
      overrideHeight="fit-content"
      title={modalTitle}
      cancelLabel={t('cancel', { keyPrefix: 'common' })}
      submitLabel={t('save', { keyPrefix: 'common' })}
      cancelTestId="StorageOverridesModal-CancelButton"
      submitTestId="StorageOverridesModal-SubmitButton"
      onClose={onClose}
      titleSeparator
      onSubmit={handleFormSubmit}
      buttonProps={{
        primary: {
          disabled: hasRowWithNoAzSelected || editUniverse.isLoading,
          dataTestId: 'StorageOverridesModal-SubmitButton'
        }
      }}
      dialogContentProps={{
        dividers: true,
        sx: {
          padding: '16px !important',
          backgroundColor: '#FFFFFF'
        }
      }}
      scroll="body"
    >
      <ModalForm>
        {fields.map((field, index) => (
          <AzCard key={field.id}>
            <AzHeader>
              <AzSelectColumn>
                <YBSelect
                  value={watch(`overrides.${index}.azUuid`) ?? ''}
                  label={t('availabilityZone')}
                  onChange={(e) => {
                    const uuid = (e.target.value as string) || '';
                    const az = uuid ? azList.find((item) => item.uuid === uuid) : null;
                    setValue(`overrides.${index}.azUuid`, uuid);
                    setValue(`overrides.${index}.azName`, az?.name ?? '');
                  }}
                  sx={{ width : '280px' }}
                  renderValue={(selected) => {
                    if (!selected) {
                      return (
                        <Typography component="span" sx={{ color: '#97A5B0' }}>
                          {t('select')}
                        </Typography>
                      );
                    }
                    const az = azList.find((item) => item.uuid === selected);
                    return az?.name ?? String(selected);
                  }}
                  dataTestId={`StorageOverridesModal-AZSelect-${index}`}
                >
                  {azList.map((az) => (
                    <MenuItem
                      key={az.uuid}
                      value={az.uuid}
                      disabled={isAzUsedByOtherRow(az.uuid, index)}
                    >
                      {az.name}
                    </MenuItem>
                  ))}
                </YBSelect>
              </AzSelectColumn>
                <TrashIcon
                  style={{ cursor: 'pointer', width: '24px', height: '24px', flexShrink: 0 }}
                  onClick={() => remove(index)}
                />
            </AzHeader>

            <ServerConfigPanel>
              <ProcessStorageFields
                index={index}
                process="tserver"
                label={t('tserver')}
                watch={watch}
                setValue={setValue}
                t={t}
              />
              {!isReadReplica && (
                <ProcessStorageFields
                  index={index}
                  process="master"
                  label={t('masterServer')}
                  watch={watch}
                  setValue={setValue}
                  t={t}
                />
              )}
            </ServerConfigPanel>
          </AzCard>
        ))}

        <YBButton
          variant="secondary"
          color="primary"
          dataTestId="StorageOverridesModal-AddAZ"
          onClick={addAvailabilityZone}
          disabled={azList.length === 0 || fields.length >= azList.length}
          startIcon={<AddCircleIcon />}
          sx={{ alignSelf: 'flex-start' }}
        >
          {t('addAvailabilityZone')}
        </YBButton>
      </ModalForm>
    </YBModal>
  );
};

export const buildStorageOverrideModalProps = (
  cluster: ReturnType<typeof getClusterByType>
): { azList: AzOption[]; initialRows: StorageOverrideRowFormValue[] } => {
  const azList = getAzListFromCluster(cluster);
  const initialRows = azNodeSpecToRows(cluster?.node_spec?.az_node_spec, azList);
  return { azList, initialRows };
};
