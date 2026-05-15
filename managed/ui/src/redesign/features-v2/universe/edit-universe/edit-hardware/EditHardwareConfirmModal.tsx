import { FC, useCallback, useMemo, useRef, useState } from 'react';
import { mui, yba, YBCheckbox } from '@yugabyte-ui-library/core';
import { useTranslation } from 'react-i18next';
import { toast } from 'react-toastify';
import pluralize from 'pluralize';
import { isEqual } from 'lodash';
import {
  countMasterAndTServerNodes,
  countRegionsAzsAndNodes,
  getClusterByType,
  mapUniversePayloadToResilienceAndRegionsProps,
  useEditUniverseContext
} from '../EditUniverseUtils';
import {
  CreateUniverseContext,
  createUniverseFormProps,
  StepsRef
} from '../../create-universe/CreateUniverseContext';
import { ResilienceAndRegionsProps } from '../../create-universe/steps/resilence-regions/dtos';
import { InstanceSettings, InstanceSettingsViewMode } from '../../create-universe/steps';
import { InstanceSettingProps } from '../../create-universe/steps/hardware-settings/dtos';
import {
  ClusterResizeNodeSpec,
  ClusterResizeStorageSpec,
  ClusterSpecClusterType,
  ClusterStorageSpec,
  NodeDetailsDedicatedTo,
  UniverseResizeNodesReqBody
} from '../../../../../v2/api/yugabyteDBAnywhereV2APIs.schemas';
import {
  useEditUniverse,
  useResizeNodes
} from '../../../../../v2/api/universe/universe';
import { createErrorMessage } from '../../../../../utils/ObjectUtils';
import {
  HardwareReviewSection,
  HardwareReviewSummary,
  ReviewHardwareChangesModal,
  hardwareReviewSectionHasVisibleChanges
} from './ReviewHardwareChangesModal';
import { DeviceInfo } from '../../../../features/universe/universe-form/utils/dto';
import { useQuery } from 'react-query';
import { QUERY_KEY, api } from '../../../../features/universe/universe-form/utils/api';
import { InstanceType } from '../../../../features/universe/universe-form/utils/dto';
import { useEditUniverseTaskHandler } from '../hooks/useEditUniverseTaskHandler';
import { buildRRInstanceSettingsFromCluster } from '../../read-replica/readReplicaUtils';

export type EditHardwareConfirmModalMode = 'cluster' | 'tserver' | 'master' | 'readReplica';

interface EditHardwareConfirmModalProps {
  visible: boolean;
  onSubmit: () => void;
  onHide: () => void;
  title?: string;
  /**
   * Drives the modal title, the Total Nodes badge label, the section of the form rendered,
   * and any extra controls (e.g. the read-replica "same as primary" checkbox).
   *
   * - `cluster`: non-dedicated primary cluster instance edit.
   * - `tserver`: dedicated T-Server only edit.
   * - `master`: dedicated Master only edit (renders the "Keep master same as T-Server" checkbox).
   * - `readReplica`: read replica cluster edit (renders the "Keep RR same as primary" checkbox).
   *
   * When omitted, the mode is inferred from `clusterType` and `isDedicatedNodes` for backward
   * compatibility.
   */
  mode?: EditHardwareConfirmModalMode;
  /** Legacy: when omitted, falls back to the target cluster's `dedicated_nodes` flag. */
  isDedicatedNodes?: boolean;
  clusterType?: ClusterSpecClusterType;
  submitLabel?: string;
  initialInstanceSettings?: Partial<InstanceSettingProps>;
  onInstanceSettingsConfirm?: (data: InstanceSettingProps) => void;
  /**
   * When true (with `onInstanceSettingsConfirm`), saving instance settings applies immediately
   * without opening Review Hardware Changes. Used for the nested master edit from the placement
   * tab's master allocation modal; the hardware tab keeps the review step.
   */
  skipHardwareReviewStep?: boolean;
}

const { YBModal } = yba;
const { Box, Typography, styled } = mui;

const TotalNodesBadge = styled(Box)(({ theme }) => ({
  display: 'inline-flex',
  alignItems: 'center',
  gap: '8px',
  background: theme.palette.common.white,
  border: `1px solid ${theme.palette.grey[300]}`,
  borderRadius: '8px',
  padding: '10px 16px',
  alignSelf: 'flex-start',
  '& .label': {
    fontSize: '13px',
    fontWeight: 500,
    color: theme.palette.grey[900]
  },
  '& .count': {
    fontSize: '13px',
    fontWeight: 500,
    color: theme.palette.grey[900]
  }
}));

/**
 * Normalized intermediate representation of a storage spec used to compare and
 * render hardware diffs. Keeps `null` for empty values so that `isEqual` works
 * across both API payload (snake_case) and form values (camelCase) sources.
 */
type NormalizedStorage = {
  volumeSize: number | null;
  numVolumes: number | null;
  diskIops: number | null;
  throughput: number | null;
  storageClass: string | null;
  storageType: string | null;
  mountPoints: string | null;
};

const normalizeStorageType = (value: unknown): string | null =>
  value === undefined || value === null ? null : String(value);

const normalizeClusterStorage = (spec: ClusterStorageSpec | undefined): NormalizedStorage => ({
  volumeSize: spec?.volume_size ?? null,
  numVolumes: spec?.num_volumes ?? null,
  diskIops: spec?.disk_iops ?? null,
  throughput: spec?.throughput ?? null,
  storageClass: spec?.storage_class ?? null,
  storageType: normalizeStorageType(spec?.storage_type),
  mountPoints: spec?.mount_points ?? null
});

const normalizeDeviceInfo = (
  deviceInfo: DeviceInfo | null | undefined
): NormalizedStorage => ({
  volumeSize: deviceInfo?.volumeSize ?? null,
  numVolumes: deviceInfo?.numVolumes ?? null,
  diskIops: deviceInfo?.diskIops ?? null,
  throughput: deviceInfo?.throughput ?? null,
  storageClass: deviceInfo?.storageClass ?? null,
  storageType: normalizeStorageType(deviceInfo?.storageType),
  mountPoints: deviceInfo?.mountPoints ?? null
});

const buildHardwareSummary = (
  instanceType: string | null | undefined,
  storage: NormalizedStorage,
  formatLabel: (code?: string | null) => string
): HardwareReviewSummary => ({
  instanceType: instanceType ?? null,
  instanceTypeLabel: formatLabel(instanceType),
  ...storage
});

const summaryFromClusterStorage = (
  instanceType: string | undefined | null,
  spec: ClusterStorageSpec | undefined,
  formatLabel: (code?: string | null) => string
): HardwareReviewSummary =>
  buildHardwareSummary(instanceType, normalizeClusterStorage(spec), formatLabel);

const summaryFromDeviceInfo = (
  instanceType: string | null | undefined,
  deviceInfo: DeviceInfo | null | undefined,
  formatLabel: (code?: string | null) => string
): HardwareReviewSummary =>
  buildHardwareSummary(instanceType, normalizeDeviceInfo(deviceInfo), formatLabel);

export const EditHardwareConfirmModal: FC<EditHardwareConfirmModalProps> = ({
  visible,
  onSubmit,
  onHide,
  title,
  mode,
  isDedicatedNodes,
  clusterType = ClusterSpecClusterType.PRIMARY,
  submitLabel,
  initialInstanceSettings,
  onInstanceSettingsConfirm,
  skipHardwareReviewStep = false
}) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'editUniverse.hardware.editHardwareModal'
  });
  const { t: tHw } = useTranslation('translation', {
    keyPrefix: 'editUniverse.hardware'
  });

  const instanceSettingsRef = useRef<StepsRef>(null);
  const resizeNodes = useResizeNodes();
  const editUniverse = useEditUniverse();
  const [pendingInstanceSettings, setPendingInstanceSettings] =
    useState<InstanceSettingProps | null>(null);
  const [reviewModalOpen, setReviewModalOpen] = useState(false);
  const [inheritPrimary, setInheritPrimary] = useState(false);
  const hasPendingChangesRef = useRef(false);
  const lastSavedInstanceSettingsRef = useRef<InstanceSettingProps | null>(null);
  const { universeData, providerRegions } = useEditUniverseContext();
  const handleEditUniverseSuccess = useEditUniverseTaskHandler(
    universeData?.info?.universe_uuid
  );
  const targetCluster = universeData ? getClusterByType(universeData, clusterType) : undefined;
  const primaryCluster = universeData
    ? getClusterByType(universeData, ClusterSpecClusterType.PRIMARY)
    : undefined;
  const providerUUID = targetCluster?.provider_spec.provider ?? primaryCluster?.provider_spec.provider;
  const providerCode =
    targetCluster?.placement_spec?.cloud_list[0].code ?? primaryCluster?.placement_spec?.cloud_list[0].code;

  // Derive the effective edit mode. When a `mode` prop is provided, it takes precedence;
  // otherwise infer from clusterType + dedicated-nodes flags so legacy callers work unchanged.
  const effectiveMode: EditHardwareConfirmModalMode = useMemo(() => {
    if (mode) return mode;
    if (clusterType === ClusterSpecClusterType.ASYNC) return 'readReplica';
    const dedicated = isDedicatedNodes ?? !!targetCluster?.node_spec.dedicated_nodes;
    return dedicated ? 'tserver' : 'cluster';
  }, [mode, clusterType, isDedicatedNodes, targetCluster?.node_spec.dedicated_nodes]);

  const useDedicatedNodes =
    effectiveMode === 'tserver' || effectiveMode === 'master'
      ? true
      : effectiveMode === 'readReplica' || effectiveMode === 'cluster'
        ? false
        : (isDedicatedNodes ?? !!targetCluster?.node_spec.dedicated_nodes);

  const instanceSettingsViewMode: InstanceSettingsViewMode =
    effectiveMode === 'master' ? 'masterOnly'
      : effectiveMode === 'tserver' ? 'tserverOnly'
        : 'all';

  const stats = primaryCluster?.placement_spec
    ? countRegionsAzsAndNodes(primaryCluster.placement_spec)
    : undefined;
  const regions =
    primaryCluster && providerRegions && stats
      ? mapUniversePayloadToResilienceAndRegionsProps(providerRegions, stats, primaryCluster)
      : undefined;

  const storageSpec = targetCluster?.node_spec.storage_spec;
  const universeUUID = universeData?.info?.universe_uuid;

  const { data: instanceTypes } = useQuery(
    [QUERY_KEY.getInstanceTypes, providerUUID, 'edit-hardware-review'],
    () => api.getInstanceTypes(providerUUID, []),
    {
      enabled: !!providerUUID
    }
  );

  const formatInstanceTypeLabel = useCallback(
    (instanceTypeCode?: string | null) => {
      if (!instanceTypeCode) return '-';
      const match = instanceTypes?.find(
        (item: InstanceType) => item.instanceTypeCode === instanceTypeCode
      );
      if (!match) return instanceTypeCode;
      if (match.numCores && match.memSizeGB) {
        return `${match.instanceTypeCode} (${pluralize('core', match.numCores, true)}, ${match.memSizeGB}GB RAM)`;
      }
      return match.instanceTypeCode;
    },
    [instanceTypes]
  );

  const toComparableStorage = (deviceInfo: DeviceInfo | null | undefined) =>
    normalizeDeviceInfo(deviceInfo);

  const toComparableSpecStorage = (spec?: ClusterStorageSpec) => normalizeClusterStorage(spec);

  const hasHardwareChanges = (settings: InstanceSettingProps) => {
    const currentTserverInstance = targetCluster?.node_spec.instance_type ?? null;
    const currentTserverStorage = toComparableSpecStorage(targetCluster?.node_spec?.storage_spec);
    const nextTserverStorage = toComparableStorage(settings.deviceInfo);
    const tserverChanged =
      (settings.instanceType ?? null) !== currentTserverInstance ||
      !isEqual(currentTserverStorage, nextTserverStorage);

    if (!useDedicatedNodes) {
      return tserverChanged;
    }

    const currentMasterInstance = targetCluster?.node_spec?.master?.instance_type ?? currentTserverInstance;
    const currentMasterStorage = toComparableSpecStorage(
      targetCluster?.node_spec?.master?.storage_spec ?? targetCluster?.node_spec?.storage_spec
    );
    const nextMasterStorage = toComparableStorage(settings.masterDeviceInfo ?? settings.deviceInfo);
    const nextMasterInstance = settings.masterInstanceType ?? settings.instanceType ?? null;

    const masterChanged =
      nextMasterInstance !== currentMasterInstance ||
      !isEqual(currentMasterStorage, nextMasterStorage);

    // User can check "keep master same as T-Server" while master already matches T-Server on
    // the cluster — instance/storage diffs are empty but the intent should still allow save.
    const keepMasterSameWithoutOtherHardwareChanges =
      (settings.keepMasterTserverSame ?? false) && !tserverChanged && !masterChanged;

    return tserverChanged || masterChanged || keepMasterSameWithoutOtherHardwareChanges;
  };

  const reviewSections: HardwareReviewSection[] = useMemo(() => {
    if (!targetCluster) return [];

    const tCurrent = summaryFromClusterStorage(
      targetCluster.node_spec.instance_type,
      targetCluster.node_spec.storage_spec,
      formatInstanceTypeLabel
    );
    const pending = pendingInstanceSettings;
    const tNext = pending
      ? summaryFromDeviceInfo(pending.instanceType, pending.deviceInfo, formatInstanceTypeLabel)
      : tCurrent;

    if (!useDedicatedNodes) {
      if (!hardwareReviewSectionHasVisibleChanges(tCurrent, tNext)) return [];
      return [{ current: tCurrent, next: tNext }];
    }

    const masterInstance =
      targetCluster.node_spec.master?.instance_type ?? targetCluster.node_spec.instance_type;
    const masterStorageSpec =
      targetCluster.node_spec.master?.storage_spec ?? targetCluster.node_spec.storage_spec;

    const mCurrent = summaryFromClusterStorage(
      masterInstance,
      masterStorageSpec,
      formatInstanceTypeLabel
    );
    const mNext = pending
      ? summaryFromDeviceInfo(
          pending.masterInstanceType ?? pending.instanceType,
          pending.masterDeviceInfo ?? pending.deviceInfo,
          formatInstanceTypeLabel
        )
      : mCurrent;

    const sections: HardwareReviewSection[] = [];
    if (hardwareReviewSectionHasVisibleChanges(tCurrent, tNext)) {
      sections.push({
        headingKey: 'tServerInstance',
        current: tCurrent,
        next: tNext
      });
    }
    if (hardwareReviewSectionHasVisibleChanges(mCurrent, mNext)) {
      sections.push({
        headingKey: 'masterServerInstance',
        current: mCurrent,
        next: mNext
      });
    }
    return sections;
  }, [targetCluster, pendingInstanceSettings, formatInstanceTypeLabel, useDedicatedNodes]);

  const toResizeStorageSpec = (
    deviceInfo: DeviceInfo | null | undefined,
    currentSpec: ClusterStorageSpec | undefined
  ): ClusterResizeStorageSpec => ({
    volume_size: deviceInfo?.volumeSize ?? currentSpec?.volume_size,
    disk_iops: deviceInfo?.diskIops ?? currentSpec?.disk_iops ?? undefined,
    throughput: deviceInfo?.throughput ?? currentSpec?.throughput ?? undefined
  });

  const toClusterStorageSpec = (
    deviceInfo: DeviceInfo | null | undefined,
    currentSpec: ClusterStorageSpec | undefined
  ): ClusterStorageSpec => ({
    volume_size: deviceInfo?.volumeSize ?? currentSpec?.volume_size ?? 0,
    num_volumes: deviceInfo?.numVolumes ?? currentSpec?.num_volumes ?? 1,
    ...(deviceInfo?.mountPoints ?? currentSpec?.mount_points
      ? { mount_points: deviceInfo?.mountPoints ?? currentSpec?.mount_points }
      : {}),
    ...(deviceInfo?.storageClass ?? currentSpec?.storage_class
      ? { storage_class: deviceInfo?.storageClass ?? currentSpec?.storage_class }
      : {}),
    ...(deviceInfo?.storageType ?? currentSpec?.storage_type
      ? { storage_type: deviceInfo?.storageType ?? currentSpec?.storage_type }
      : {}),
    ...(deviceInfo?.diskIops !== undefined && deviceInfo?.diskIops !== null
      ? { disk_iops: deviceInfo.diskIops }
      : currentSpec?.disk_iops !== undefined && currentSpec?.disk_iops !== null
        ? { disk_iops: currentSpec.disk_iops }
        : {}),
    ...(deviceInfo?.throughput !== undefined && deviceInfo?.throughput !== null
      ? { throughput: deviceInfo.throughput }
      : currentSpec?.throughput !== undefined && currentSpec?.throughput !== null
        ? { throughput: currentSpec.throughput }
        : {}),
    ...(currentSpec?.cloud_volume_encryption
      ? { cloud_volume_encryption: currentSpec.cloud_volume_encryption }
      : {})
  });

  const buildResizeNodeSpec = (
    settings: InstanceSettingProps,
    dedicatedNodes: boolean
  ): ClusterResizeNodeSpec => {
    const currentTserverInstance = targetCluster?.node_spec?.instance_type ?? null;
    const tserverInstanceType = settings.instanceType ?? currentTserverInstance;
    const currentTserverStorageSpec = targetCluster?.node_spec?.storage_spec;
    const tserverStorageSpec = toResizeStorageSpec(settings.deviceInfo, currentTserverStorageSpec);
    const tserverChanged =
      tserverInstanceType !== currentTserverInstance ||
      !isEqual(
        toComparableSpecStorage(currentTserverStorageSpec),
        toComparableStorage(settings.deviceInfo)
      );

    if (!dedicatedNodes) {
      return {
        instance_type: tserverInstanceType ?? undefined,
        storage_spec: tserverStorageSpec
      };
    }

    const currentMasterInstance =
      targetCluster?.node_spec?.master?.instance_type ?? currentTserverInstance;
    const masterInstanceType = settings.masterInstanceType ?? tserverInstanceType;
    const currentMasterStorageSpec =
      targetCluster?.node_spec?.master?.storage_spec ?? currentTserverStorageSpec;
    const masterStorageSpec = toResizeStorageSpec(
      settings.masterDeviceInfo ?? settings.deviceInfo,
      currentMasterStorageSpec
    );
    const masterChanged =
      masterInstanceType !== currentMasterInstance ||
      !isEqual(
        toComparableSpecStorage(currentMasterStorageSpec),
        toComparableStorage(settings.masterDeviceInfo ?? settings.deviceInfo)
      );

    const dedicatedNodeSpec: ClusterResizeNodeSpec = {};
    if (tserverChanged) {
      dedicatedNodeSpec.instance_type = tserverInstanceType ?? undefined;
      dedicatedNodeSpec.storage_spec = tserverStorageSpec;
      dedicatedNodeSpec.tserver = {
        instance_type: tserverInstanceType ?? undefined,
        storage_spec: tserverStorageSpec
      };
    }
    if (masterChanged) {
      dedicatedNodeSpec.master = {
        instance_type: masterInstanceType ?? undefined,
        storage_spec: masterStorageSpec
      };
    }

    return dedicatedNodeSpec;
  };

  const buildEditNodeSpec = (settings: InstanceSettingProps, dedicatedNodes: boolean) => {
    const currentTserverInstance = targetCluster?.node_spec?.instance_type ?? null;
    const tserverInstanceType = settings.instanceType ?? currentTserverInstance;
    const currentTserverStorageSpec = targetCluster?.node_spec?.storage_spec;
    const tserverStorageSpec = toClusterStorageSpec(settings.deviceInfo, currentTserverStorageSpec);

    if (!dedicatedNodes) {
      return {
        instance_type: tserverInstanceType ?? undefined,
        storage_spec: tserverStorageSpec
      };
    }

    const currentMasterInstance =
      targetCluster?.node_spec?.master?.instance_type ?? currentTserverInstance;
    const masterInstanceType = settings.masterInstanceType ?? tserverInstanceType;
    const currentMasterStorageSpec =
      targetCluster?.node_spec?.master?.storage_spec ?? currentTserverStorageSpec;
    const masterStorageSpec = toClusterStorageSpec(
      settings.masterDeviceInfo ?? settings.deviceInfo,
      currentMasterStorageSpec
    );

    return {
      ...(tserverInstanceType ? { instance_type: tserverInstanceType } : {}),
      storage_spec: tserverStorageSpec,
      tserver: {
        ...(tserverInstanceType ? { instance_type: tserverInstanceType } : {}),
        storage_spec: tserverStorageSpec
      },
      master: {
        ...(masterInstanceType ? { instance_type: masterInstanceType } : {}),
        storage_spec: masterStorageSpec
      }
    };
  };

  const hasNumVolumesChange = (settings: InstanceSettingProps, dedicatedNodes: boolean) => {
    const currentTserverVolumes = targetCluster?.node_spec?.storage_spec?.num_volumes ?? null;
    const nextTserverVolumes = settings.deviceInfo?.numVolumes ?? currentTserverVolumes;
    if (nextTserverVolumes !== currentTserverVolumes) {
      return true;
    }

    if (!dedicatedNodes) {
      return false;
    }

    const currentMasterVolumes =
      targetCluster?.node_spec?.master?.storage_spec?.num_volumes ?? currentTserverVolumes;
    const nextMasterVolumes =
      settings.masterDeviceInfo?.numVolumes ?? settings.deviceInfo?.numVolumes ?? currentMasterVolumes;
    return nextMasterVolumes !== currentMasterVolumes;
  };

  const submitEditUniverse = (settings: InstanceSettingProps) => {
    if (!universeUUID || !targetCluster?.uuid) {
      toast.error(t('unableToApplyChanges'));
      return;
    }

    editUniverse.mutate(
      {
        uniUUID: universeUUID,
        data: {
          expected_universe_version: -1,
          clusters: [
            {
              uuid: targetCluster.uuid,
              node_spec: buildEditNodeSpec(settings, !!useDedicatedNodes)
            }
          ]
        }
      },
      {
        onSuccess: (response) => {
          handleEditUniverseSuccess(response.task_uuid);
          onSubmit();
          resetAndClose();
        },
        onError: (error: unknown) => {
          toast.error(createErrorMessage(error));
        }
      }
    );
  };

  const resetAndClose = () => {
    setPendingInstanceSettings(null);
    lastSavedInstanceSettingsRef.current = null;
    setReviewModalOpen(false);
    setInheritPrimary(false);
    onHide();
  };

  const confirmResizeNodes = (delaySeconds: number) => {
    if (!pendingInstanceSettings || !universeUUID || !targetCluster?.uuid) {
      toast.error(t('unableToApplyChanges'));
      return;
    }

    if (onInstanceSettingsConfirm) {
      onInstanceSettingsConfirm(pendingInstanceSettings);
      onSubmit();
      resetAndClose();
      return;
    }

    if (
      clusterType === ClusterSpecClusterType.ASYNC ||
      hasNumVolumesChange(pendingInstanceSettings, !!useDedicatedNodes)
    ) {
      submitEditUniverse(pendingInstanceSettings);
      return;
    }

    const payload: UniverseResizeNodesReqBody = {
      clusters: [
        {
          uuid: targetCluster.uuid,
          node_spec: buildResizeNodeSpec(pendingInstanceSettings, !!useDedicatedNodes)
        }
      ],
      sleep_after_master_restart_millis: delaySeconds * 1000,
      sleep_after_tserver_restart_millis: delaySeconds * 1000,
      roll_max_batch_size: universeData?.info?.roll_max_batch_size
    };

    resizeNodes.mutate(
      {
        uniUUID: universeUUID,
        data: payload
      },
      {
        onSuccess: (response) => {
          handleEditUniverseSuccess(response.task_uuid);
          onSubmit();
          resetAndClose();
        },
        onError: (error: unknown) => {
          toast.error(createErrorMessage(error));
        }
      }
    );
  };

  if (!visible || !universeData || !providerRegions || !targetCluster || !regions) {
    return null;
  }

  const defaultInstanceSettings = {
    arch: universeData?.info?.arch,
    instanceType: targetCluster?.node_spec.instance_type,
    masterInstanceType:
      targetCluster?.node_spec.master?.instance_type ?? targetCluster?.node_spec.instance_type,
    useSpotInstance: targetCluster?.use_spot_instance,
    keepMasterTserverSame: false,
    deviceInfo: {
      volumeSize: storageSpec?.volume_size,
      numVolumes: storageSpec?.num_volumes,
      diskIops: storageSpec?.disk_iops,
      throughput: storageSpec?.throughput,
      storageClass: storageSpec?.storage_class,
      storageType: storageSpec?.storage_type,
      mountPoints: storageSpec?.mount_points
    },
    masterDeviceInfo: {
      volumeSize: targetCluster?.node_spec?.master?.storage_spec?.volume_size ?? storageSpec?.volume_size,
      numVolumes: targetCluster?.node_spec?.master?.storage_spec?.num_volumes ?? storageSpec?.num_volumes,
      diskIops: targetCluster?.node_spec?.master?.storage_spec?.disk_iops ?? storageSpec?.disk_iops,
      throughput: targetCluster?.node_spec?.master?.storage_spec?.throughput ?? storageSpec?.throughput,
      storageClass:
        targetCluster?.node_spec?.master?.storage_spec?.storage_class ?? storageSpec?.storage_class,
      storageType:
        targetCluster?.node_spec?.master?.storage_spec?.storage_type ?? storageSpec?.storage_type,
      mountPoints:
        targetCluster?.node_spec?.master?.storage_spec?.mount_points ?? storageSpec?.mount_points
    }
  } as InstanceSettingProps;

  // For the read-replica edit modal, when the user toggles "Keep RR same as primary T-Server",
  // we replace the form defaults with values derived from the primary cluster's T-Server
  // node_spec. This drives both what the form renders and what we submit on apply.
  const primaryDerivedSettings: Partial<InstanceSettingProps> | null =
    effectiveMode === 'readReplica' && primaryCluster && universeData?.info?.arch
      ? buildRRInstanceSettingsFromCluster(primaryCluster, universeData.info.arch, true)
      : null;

  const isInheritingPrimary =
    effectiveMode === 'readReplica' && inheritPrimary && !!primaryDerivedSettings;

  const seedSettings: Partial<InstanceSettingProps> | undefined = isInheritingPrimary
    ? primaryDerivedSettings ?? undefined
    : initialInstanceSettings;

  const mergedInstanceSettings = {
    ...defaultInstanceSettings,
    ...seedSettings,
    deviceInfo: {
      ...defaultInstanceSettings.deviceInfo,
      ...seedSettings?.deviceInfo
    },
    masterDeviceInfo: {
      ...defaultInstanceSettings.masterDeviceInfo,
      ...seedSettings?.masterDeviceInfo
    }
  } as InstanceSettingProps;

  // Compute the Total Nodes badge label and count based on the effective edit mode.
  const masterTserverCounts =
    universeData && primaryCluster?.placement_spec
      ? countMasterAndTServerNodes(universeData, primaryCluster.placement_spec)
      : null;

  const readReplicaStats =
    effectiveMode === 'readReplica' && targetCluster?.placement_spec
      ? countRegionsAzsAndNodes(targetCluster.placement_spec)
      : null;

  const badgeLabel = (() => {
    switch (effectiveMode) {
      case 'tserver':
        return t('totalTServerNodes');
      case 'master':
        return t('totalMasterServerNodes');
      case 'cluster':
      case 'readReplica':
      default:
        return t('totalNodes');
    }
  })();

  const badgeCount = (() => {
    switch (effectiveMode) {
      case 'tserver':
        return masterTserverCounts?.[NodeDetailsDedicatedTo.TSERVER] ?? 0;
      case 'master':
        return masterTserverCounts?.[NodeDetailsDedicatedTo.MASTER] ?? 0;
      case 'readReplica':
        return readReplicaStats?.totalNodes ?? 0;
      case 'cluster':
      default:
        return stats?.totalNodes ?? 0;
    }
  })();

  const modalTitle = title ?? (() => {
    switch (effectiveMode) {
      case 'tserver':
        return tHw('tServerInstance');
      case 'master':
        return tHw('masterServerInstance');
      case 'readReplica':
        return t('rrInstance', { keyPrefix: 'readReplica.addRR' });
      case 'cluster':
      default:
        return t('title');
    }
  })();

  return (
    <>
      <YBModal
        open={visible}
        onSubmit={() => {
          instanceSettingsRef.current?.onNext();
        }}
        onClose={resetAndClose}
        title={modalTitle}
        dialogContentProps={{ sx: { padding: '16px !important' } }}
        size="md"
        submitLabel={submitLabel ?? t('submitLabel')}
        cancelLabel={t('cancel', { keyPrefix: 'common' })}
        overrideHeight={'fit-content'}
        titleSeparator
      >
        <Box sx={{ display: 'flex', flexDirection: 'column', gap: '16px' }}>
          <TotalNodesBadge>
            <Typography component="span" className="label">
              {badgeLabel}
            </Typography>
            <Typography component="span" className="count">
              {badgeCount}
            </Typography>
          </TotalNodesBadge>
          {effectiveMode === 'readReplica' && (
            <YBCheckbox
              dataTestId="edit-hardware-rr-same-as-primary"
              checked={inheritPrimary}
              onChange={(_, checked) => setInheritPrimary(checked)}
              label={t('primaryRRInstanceSame', { keyPrefix: 'readReplica.addRR' })}
              size="large"
            />
          )}
          <CreateUniverseContext.Provider
            value={
              ([
                {
                  activeStep: 1,
                  resilienceAndRegionsSettings: regions,
                  generalSettings: {
                    providerConfiguration: {
                      uuid: providerUUID,
                      code: providerCode
                    }
                  },
                  nodesAvailabilitySettings: {
                    useDedicatedNodes: !!useDedicatedNodes
                  },
                  instanceSettings: mergedInstanceSettings
                },
                {
                  setResilienceType: () => {},
                  saveResilienceAndRegionsSettings: (_data: ResilienceAndRegionsProps) => {},
                  saveInstanceSettings: (data: InstanceSettingProps) => {
                    setPendingInstanceSettings(data);
                    lastSavedInstanceSettingsRef.current = data;
                    hasPendingChangesRef.current = hasHardwareChanges(data);
                  },
                  moveToNextPage: () => {
                    if (!hasPendingChangesRef.current) {
                      toast.warn(t('noHardwareChanges'));
                      return;
                    }
                    if (
                      skipHardwareReviewStep &&
                      onInstanceSettingsConfirm &&
                      lastSavedInstanceSettingsRef.current
                    ) {
                      onInstanceSettingsConfirm(lastSavedInstanceSettingsRef.current);
                      onSubmit();
                      resetAndClose();
                      return;
                    }
                    setReviewModalOpen(true);
                  },
                  moveToPreviousPage: () => {}
                }
              ] as unknown) as createUniverseFormProps
            }
          >
            {/*
              `key` forces InstanceSettings to remount whenever the read-replica
              "Keep same as primary" toggle flips, so `useForm`'s defaultValues
              pick up the swapped seed (primary T-Server vs. RR cluster).
            */}
            <InstanceSettings
              key={isInheritingPrimary ? 'inherit-primary' : 'rr-cluster'}
              editMode={true}
              viewMode={instanceSettingsViewMode}
              disableTserverFields={isInheritingPrimary}
              ref={instanceSettingsRef}
            />
          </CreateUniverseContext.Provider>
        </Box>
      </YBModal>
      <ReviewHardwareChangesModal
        visible={reviewModalOpen}
        sections={reviewSections}
        isSubmitting={resizeNodes.isLoading || editUniverse.isLoading}
        onClose={() => setReviewModalOpen(false)}
        onConfirm={confirmResizeNodes}
      />
    </>
  );
};
