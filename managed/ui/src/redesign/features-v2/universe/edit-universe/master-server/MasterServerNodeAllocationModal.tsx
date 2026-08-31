import { FC, useEffect, useMemo, useState } from 'react';
import { browserHistory } from 'react-router';
import { mui, yba } from '@yugabyte-ui-library/core';
import { useTranslation } from 'react-i18next';
import { DedicatedNode } from '../../create-universe/steps/nodes-availability/DedicatedNodes';
import {
  CreateUniverseContext,
  createUniverseFormProps
} from '../../create-universe/CreateUniverseContext';
import { EditPlacementContextProps } from '../edit-placement/EditPlacementContext';
import {
  countRegionsAzsAndNodes,
  getClusterByType,
  getNodeAvailabilityDefaultsFromClusterPlacement,
  mapUniversePayloadToResilienceAndRegionsProps,
  useEditUniverseContext
} from '../EditUniverseUtils';
import { FormProvider, useForm } from 'react-hook-form';
import { NodeAvailabilityProps } from '../../create-universe/steps/nodes-availability/dtos';
import { REPLICATION_FACTOR } from '../../create-universe/fields/FieldNames';
import { ClusterSpecClusterType } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import EditIcon from '@app/redesign/assets/edit2.svg';
import { EditHardwareConfirmModal } from '../edit-hardware/EditHardwareConfirmModal';
import { InstanceSettingProps } from '../../create-universe/steps/hardware-settings/dtos';
import { EditUniverseTabs } from '../EditUniverseContext';
import { getEditUniverseSettingsRoute } from '../editUniverseTabUtils';

interface MasterServerNodeAllocationModalProps {
  visible: boolean;
  onClose: () => void;
  onApply: (data: NodeAvailabilityProps, instanceSettings?: InstanceSettingProps | null) => void;
  isSubmitting?: boolean;
  /** When set, placement + resilience use this geo partition (default partition if omitted is resolved in parent for geo). */
  selectedPartitionUUID?: string;
}

const { YBModal } = yba;
const { styled, Box, Typography } = mui;

const StyledPanel = styled('div')(({ theme }) => ({
  border: `1px solid ${theme.palette.grey[300]}`,
  borderRadius: '8px',
  display: 'flex',
  flexDirection: 'column',
  gap: '0px'
}));

const StyledEditMasterServerPanel = styled('div')(({ theme }) => ({
  border: `1px solid ${theme.palette.grey[300]}`,
  borderRadius: '8px',
  display: 'flex',
  flexDirection: 'column',
  marginLeft: '58px',
  marginTop: '8px',
  marginBottom: '24px',
  padding: '8px 16px',
  marginRight: '24px'
}));

const Header = styled(Box)(({ theme }) => ({
  display: 'flex',
  flexDirection: 'row',
  gap: '4px',
  padding: '10px 8px',
  alignItems: 'center',
  color: theme.palette.primary[600],
  cursor: 'pointer'
}));

const SettingsHardwareLink = styled(Typography)(({ theme }) => ({
  fontWeight: 600,
  color: theme.palette.primary[600],
  cursor: 'pointer',
  '&:hover': {
    textDecoration: 'underline'
  }
}));

export const MasterServerNodeAllocationModal: FC<MasterServerNodeAllocationModalProps> = ({
  visible,
  onClose,
  onApply,
  isSubmitting = false,
  selectedPartitionUUID
}) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'editUniverse.masterServerNodeAllocationModal'
  });
  const { universeData, providerRegions } = useEditUniverseContext();
  const [editHardwareOpen, setEditHardwareOpen] = useState(false);
  const [pendingInstanceSettings, setPendingInstanceSettings] =
    useState<InstanceSettingProps | null>(null);

  const primaryCluster = universeData
    ? getClusterByType(universeData, ClusterSpecClusterType.PRIMARY)
    : undefined;
  const wasAlreadyDedicated = !!primaryCluster?.node_spec?.dedicated_nodes;
  const universeUuid = universeData?.info?.universe_uuid ?? '';

  const resolvedPartition = useMemo(() => {
    if (!primaryCluster?.partitions_spec?.length) return undefined;
    const uuid =
      selectedPartitionUUID ??
      primaryCluster.partitions_spec.find((p) => p.default_partition)?.uuid ??
      primaryCluster.partitions_spec[0]?.uuid;
    return primaryCluster.partitions_spec.find((p) => p.uuid === uuid);
  }, [primaryCluster, selectedPartitionUUID]);

  const placementForModal = useMemo(
    () => resolvedPartition?.placement ?? primaryCluster?.placement_spec,
    [resolvedPartition, primaryCluster]
  );

  const resilienceEntity = resolvedPartition ?? primaryCluster;

  const regions = useMemo(() => {
    if (!universeData || !providerRegions?.length || !placementForModal || !resilienceEntity) {
      return undefined;
    }
    const stats = countRegionsAzsAndNodes(placementForModal);
    return mapUniversePayloadToResilienceAndRegionsProps(providerRegions, stats, resilienceEntity);
  }, [universeData, providerRegions, placementForModal, resilienceEntity]);

  const formDefaults = useMemo(() => {
    if (!primaryCluster || !placementForModal) return undefined;
    return getNodeAvailabilityDefaultsFromClusterPlacement(
      primaryCluster,
      placementForModal,
      resolvedPartition?.replication_factor
    );
  }, [primaryCluster, placementForModal, resolvedPartition?.replication_factor]);

  const emptyFormDefaults: NodeAvailabilityProps = {
    availabilityZones: {},
    useDedicatedNodes: false,
    [REPLICATION_FACTOR]: 1
  };

  const methods = useForm<NodeAvailabilityProps>({
    defaultValues: formDefaults ?? emptyFormDefaults
  });

  useEffect(() => {
    if (visible && formDefaults) {
      methods.reset(formDefaults);
    }
  }, [visible, formDefaults]); // eslint-disable-line react-hooks/exhaustive-deps -- reset when modal opens / placement changes

  const { watch, handleSubmit } = methods;
  const enableDedicatedNodes = watch('useDedicatedNodes');
  const canEditMasterInstance = enableDedicatedNodes && !wasAlreadyDedicated;

  if (!visible) {
    return null;
  }

  if (
    !universeData ||
    !providerRegions?.length ||
    !placementForModal ||
    !regions ||
    !formDefaults
  ) {
    return null;
  }

  const handleClose = () => {
    setPendingInstanceSettings(null);
    setEditHardwareOpen(false);
    onClose();
  };

  const handleNavigateToHardware = () => {
    handleClose();
    if (universeUuid) {
      browserHistory.push(getEditUniverseSettingsRoute(universeUuid, EditUniverseTabs.HARDWARE));
    }
  };

  return (
    <>
      <YBModal
        open={visible}
        onClose={handleClose}
        onSubmit={handleSubmit((data) => onApply(data, pendingInstanceSettings))}
        title={t('title')}
        submitLabel={t('applyChanges', { keyPrefix: 'common' })}
        cancelLabel={t('cancel', { keyPrefix: 'common' })}
        size="md"
        titleSeparator
        buttonProps={{
          primary: {
            dataTestId: 'master-server-allocation-apply',
            disabled: isSubmitting
          }
        }}
        dialogContentProps={{
          sx: {
            padding: '24px 16px !important'
          }
        }}
      >
        <CreateUniverseContext.Provider
          value={
            [
              {
                activeStep: 1,
                resilienceAndRegionsSettings: regions
              },
              {
                saveNodesAvailabilitySettings: (
                  data: EditPlacementContextProps['nodesAndAvailability']
                ) => {},
                moveToNextPage: () => {}
              }
            ] as unknown as createUniverseFormProps
          }
        >
          <FormProvider {...methods}>
            <StyledPanel>
              <div>
                <DedicatedNode noAccordion />
              </div>
              {enableDedicatedNodes && (
                <StyledEditMasterServerPanel>
                  {canEditMasterInstance && (
                    <Header
                      onClick={(e) => {
                        e.preventDefault();
                        e.stopPropagation();
                        setEditHardwareOpen(true);
                      }}
                    >
                      <EditIcon />
                      <Typography variant="body1">{t('editMasterServer')}</Typography>
                    </Header>
                  )}
                  <Box
                    sx={(theme) => ({
                      marginLeft: canEditMasterInstance ? '36px' : 0,
                      display: 'flex',
                      flexDirection: 'column',
                      gap: '2px',
                      color: theme.palette.grey[700]
                    })}
                  >
                    <Typography variant="subtitle1">{t('subText')}</Typography>
                    <SettingsHardwareLink
                      variant="subtitle2"
                      onClick={(e) => {
                        e.preventDefault();
                        e.stopPropagation();
                        handleNavigateToHardware();
                      }}
                    >
                      {t('settingsAndHardware')}
                    </SettingsHardwareLink>
                  </Box>
                </StyledEditMasterServerPanel>
              )}
            </StyledPanel>
          </FormProvider>
        </CreateUniverseContext.Provider>
      </YBModal>
      {canEditMasterInstance && (
        <EditHardwareConfirmModal
          visible={editHardwareOpen}
          title={t('masterServerInstance', { keyPrefix: 'editUniverse.hardware' })}
          onSubmit={() => {
            setEditHardwareOpen(false);
          }}
          onHide={() => setEditHardwareOpen(false)}
          mode="master"
          isDedicatedNodes={enableDedicatedNodes}
          submitLabel={t('save', { keyPrefix: 'common' })}
          initialInstanceSettings={pendingInstanceSettings ?? undefined}
          onInstanceSettingsConfirm={(data) => setPendingInstanceSettings(data)}
          skipHardwareReviewStep
        />
      )}
    </>
  );
};
