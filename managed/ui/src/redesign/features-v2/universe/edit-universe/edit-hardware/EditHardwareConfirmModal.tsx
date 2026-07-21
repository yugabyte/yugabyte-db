import { FC, useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { mui, yba, YBCheckbox } from '@yugabyte-ui-library/core';
import { useTranslation } from 'react-i18next';
import pluralize from 'pluralize';
import { isEqual } from 'lodash';
import {
  countMasterAndTServerNodes,
  countRegionsAzsAndNodes,
  getClusterByType,
  getK8sResourceSpecFromNodeSpec,
  isKubernetesCluster,
  mapUniversePayloadToResilienceAndRegionsProps,
  useEditUniverseContext
} from '../EditUniverseUtils';
import {
  CreateUniverseContext,
  createUniverseFormProps,
  StepsRef
} from '../../create-universe/CreateUniverseContext';
import { TotalNodesBadge } from '../../create-universe/components/TotalNodesBadge';
import { ResilienceAndRegionsProps } from '../../create-universe/steps/resilence-regions/dtos';
import { InstanceSettings, InstanceSettingsViewMode } from '../../create-universe/steps';
import { InstanceSettingProps } from '../../create-universe/steps/hardware-settings/dtos';
import {
  ClusterResizeNodeSpec,
  ClusterNodeSpec,
  ClusterSpecClusterType,
  ClusterStorageSpec,
  K8SNodeResourceSpec,
  NodeDetailsDedicatedTo,
  ResizeUpdateOption,
  UniverseResizeNodesReqBody
} from '../../../../../v2/api/yugabyteDBAnywhereV2APIs.schemas';
import {
  useCheckResizeOptions,
  useEditUniverse,
  useResizeNodes
} from '../../../../../v2/api/universe/universe';
import { createErrorMessage } from '../../../../../utils/ObjectUtils';
import {
  canUseFullMove,
  canUseRollingResize,
  HardwareReviewSection,
  HardwareReviewSummary,
  ReviewHardwareChangesModal,
  ReviewHardwareConfirmPayload,
  hardwareReviewSectionHasVisibleChanges
} from './ReviewHardwareChangesModal';
import {
  NormalizedStorage,
  normalizeClusterStorage,
  normalizeDeviceInfo,
  toClusterStorageSpec,
  toResizeStorageSpec
} from './EditHardwareStorageUtils';
import { DeviceInfo, K8NodeSpec } from '../../../../features/universe/universe-form/utils/dto';
import { useQuery } from 'react-query';
import { QUERY_KEY, api } from '../../../../features/universe/universe-form/utils/api';
import { InstanceType } from '../../../../features/universe/universe-form/utils/dto';
import { useEditUniverseTaskHandler } from '../hooks/useEditUniverseTaskHandler';
import { buildRRInstanceSettingsFromCluster } from '../../read-replica/readReplicaUtils';
import { useYBToast } from '../../create-universe/helpers/ToastUtils';

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
   * - `readReplica`: read replica cluster edit (renders the "Keep RR same as primary" checkbox;
   *   T-Server section only — no Master accordion, even on dedicated-node universes).
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
const { Box } = mui;

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

const summaryFromK8s = (
  resourceSpec: K8NodeSpec | null | undefined,
  deviceInfo: DeviceInfo | null | undefined
): HardwareReviewSummary => ({
  instanceType: null,
  instanceTypeLabel: '-',
  ...normalizeDeviceInfo(deviceInfo),
  cpuCoreCount: resourceSpec?.cpuCoreCount ?? null,
  memoryGib: resourceSpec?.memoryGib ?? null
});

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
  const checkResizeOptions = useCheckResizeOptions();
  const [pendingInstanceSettings, setPendingInstanceSettings] =
    useState<InstanceSettingProps | null>(null);
  const [reviewModalOpen, setReviewModalOpen] = useState(false);
  const [resizeOptions, setResizeOptions] = useState<ResizeUpdateOption[] | undefined>(undefined);
  const [isLoadingResizeOptions, setIsLoadingResizeOptions] = useState(false);
  const [inheritPrimary, setInheritPrimary] = useState(false);
  const hasPendingChangesRef = useRef(false);
  const lastSavedInstanceSettingsRef = useRef<InstanceSettingProps | null>(null);
  const { universeData, providerRegions } = useEditUniverseContext();
  const handleEditUniverseSuccess = useEditUniverseTaskHandler(universeData?.info?.universe_uuid);
  const targetCluster = universeData ? getClusterByType(universeData, clusterType) : undefined;
  const primaryCluster = universeData
    ? getClusterByType(universeData, ClusterSpecClusterType.PRIMARY)
    : undefined;
  const providerUUID =
    targetCluster?.provider_spec.provider ?? primaryCluster?.provider_spec.provider;
  const providerCode =
    targetCluster?.placement_spec?.cloud_list[0].code ??
    primaryCluster?.placement_spec?.cloud_list[0].code;
  const isK8s = isKubernetesCluster(targetCluster ?? primaryCluster);
  const universeUUID = universeData?.info?.universe_uuid;

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
    effectiveMode === 'master'
      ? 'masterOnly'
      : effectiveMode === 'tserver' || effectiveMode === 'readReplica'
        ? 'tserverOnly'
        : 'all';

  const stats = primaryCluster?.placement_spec
    ? countRegionsAzsAndNodes(primaryCluster.placement_spec)
    : undefined;
  const regions =
    primaryCluster && providerRegions && stats
      ? mapUniversePayloadToResilienceAndRegionsProps(providerRegions, stats, primaryCluster)
      : undefined;

  const storageSpec = targetCluster?.node_spec.storage_spec;
  const toast = useYBToast();

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

  const toComparableK8s = (spec: K8NodeSpec | null | undefined): K8NodeSpec | null =>
    spec ? { cpuCoreCount: spec.cpuCoreCount, memoryGib: spec.memoryGib } : null;

  const toK8sResourceSpec = (
    spec: K8NodeSpec | null | undefined
  ): K8SNodeResourceSpec | undefined =>
    spec
      ? {
          cpu_core_count: spec.cpuCoreCount,
          memory_gib: spec.memoryGib
        }
      : undefined;

  const hasHardwareChanges = (settings: InstanceSettingProps) => {
    if (isK8s) {
      const currentTserverK8s = getK8sResourceSpecFromNodeSpec(targetCluster?.node_spec, 'tserver');
      const currentMasterK8s =
        getK8sResourceSpecFromNodeSpec(targetCluster?.node_spec, 'master') ?? currentTserverK8s;
      const nextTserverK8s = settings.tserverK8SNodeResourceSpec ?? null;
      const nextMasterK8s =
        (settings.keepMasterTserverSame
          ? nextTserverK8s
          : (settings.masterK8SNodeResourceSpec ?? nextTserverK8s)) ?? null;
      const currentTserverInstance = targetCluster?.node_spec.instance_type ?? null;
      const currentMasterInstance =
        targetCluster?.node_spec.master?.instance_type ?? currentTserverInstance;
      const tserverInstanceChanged = (settings.instanceType ?? null) !== currentTserverInstance;
      const masterInstanceChanged =
        (settings.masterInstanceType ?? settings.instanceType ?? null) !== currentMasterInstance;
      const currentTserverStorage = toComparableSpecStorage(targetCluster?.node_spec?.storage_spec);
      const nextTserverStorage = toComparableStorage(settings.deviceInfo);
      const currentMasterStorage = toComparableSpecStorage(
        targetCluster?.node_spec?.master?.storage_spec ?? targetCluster?.node_spec?.storage_spec
      );
      const nextMasterStorage = toComparableStorage(
        settings.keepMasterTserverSame
          ? settings.deviceInfo
          : (settings.masterDeviceInfo ?? settings.deviceInfo)
      );
      const tserverStorageChanged = !isEqual(currentTserverStorage, nextTserverStorage);
      const masterStorageChanged = !isEqual(currentMasterStorage, nextMasterStorage);

      if (effectiveMode === 'master') {
        return (
          masterInstanceChanged ||
          !isEqual(toComparableK8s(currentMasterK8s), toComparableK8s(nextMasterK8s)) ||
          masterStorageChanged
        );
      }
      if (effectiveMode === 'tserver' || effectiveMode === 'readReplica') {
        return (
          tserverInstanceChanged ||
          !isEqual(toComparableK8s(currentTserverK8s), toComparableK8s(nextTserverK8s)) ||
          tserverStorageChanged
        );
      }
      return (
        tserverInstanceChanged ||
        masterInstanceChanged ||
        !isEqual(toComparableK8s(currentTserverK8s), toComparableK8s(nextTserverK8s)) ||
        !isEqual(toComparableK8s(currentMasterK8s), toComparableK8s(nextMasterK8s)) ||
        tserverStorageChanged ||
        masterStorageChanged
      );
    }

    const currentTserverInstance = targetCluster?.node_spec.instance_type ?? null;
    const currentTserverStorage = toComparableSpecStorage(targetCluster?.node_spec?.storage_spec);
    const nextTserverStorage = toComparableStorage(settings.deviceInfo);
    const tserverChanged =
      (settings.instanceType ?? null) !== currentTserverInstance ||
      !isEqual(currentTserverStorage, nextTserverStorage);

    if (!useDedicatedNodes) {
      return tserverChanged;
    }

    const currentMasterInstance =
      targetCluster?.node_spec?.master?.instance_type ?? currentTserverInstance;
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

    const currentTserverK8s = getK8sResourceSpecFromNodeSpec(targetCluster.node_spec, 'tserver');
    const pending = pendingInstanceSettings;
    if (isK8s && (currentTserverK8s || pending?.tserverK8SNodeResourceSpec)) {
      const currentMasterK8s =
        getK8sResourceSpecFromNodeSpec(targetCluster.node_spec, 'master') ?? currentTserverK8s;
      const tCurrent = summaryFromK8s(currentTserverK8s, {
        volumeSize: targetCluster.node_spec.storage_spec?.volume_size,
        numVolumes: targetCluster.node_spec.storage_spec?.num_volumes,
        diskIops: targetCluster.node_spec.storage_spec?.disk_iops ?? null,
        throughput: targetCluster.node_spec.storage_spec?.throughput ?? null,
        storageClass: targetCluster.node_spec.storage_spec?.storage_class ?? 'standard',
        storageType: targetCluster.node_spec.storage_spec?.storage_type ?? null,
        mountPoints: targetCluster.node_spec.storage_spec?.mount_points
      } as DeviceInfo);
      const tNext = pending
        ? summaryFromK8s(pending.tserverK8SNodeResourceSpec, pending.deviceInfo)
        : tCurrent;

      if (!useDedicatedNodes && effectiveMode !== 'cluster') {
        if (!hardwareReviewSectionHasVisibleChanges(tCurrent, tNext)) return [];
        return [{ current: tCurrent, next: tNext }];
      }

      const mCurrent = summaryFromK8s(currentMasterK8s, {
        volumeSize:
          targetCluster.node_spec.master?.storage_spec?.volume_size ??
          targetCluster.node_spec.storage_spec?.volume_size,
        numVolumes:
          targetCluster.node_spec.master?.storage_spec?.num_volumes ??
          targetCluster.node_spec.storage_spec?.num_volumes,
        diskIops:
          targetCluster.node_spec.master?.storage_spec?.disk_iops ??
          targetCluster.node_spec.storage_spec?.disk_iops ??
          null,
        throughput:
          targetCluster.node_spec.master?.storage_spec?.throughput ??
          targetCluster.node_spec.storage_spec?.throughput ??
          null,
        storageClass:
          targetCluster.node_spec.master?.storage_spec?.storage_class ??
          targetCluster.node_spec.storage_spec?.storage_class ??
          'standard',
        storageType:
          targetCluster.node_spec.master?.storage_spec?.storage_type ??
          targetCluster.node_spec.storage_spec?.storage_type ??
          null,
        mountPoints:
          targetCluster.node_spec.master?.storage_spec?.mount_points ??
          targetCluster.node_spec.storage_spec?.mount_points
      } as DeviceInfo);
      const mNext = pending
        ? summaryFromK8s(
            pending.keepMasterTserverSame
              ? pending.tserverK8SNodeResourceSpec
              : (pending.masterK8SNodeResourceSpec ?? pending.tserverK8SNodeResourceSpec),
            pending.masterDeviceInfo ?? pending.deviceInfo
          )
        : mCurrent;

      const sections: HardwareReviewSection[] = [];
      if (effectiveMode !== 'master' && hardwareReviewSectionHasVisibleChanges(tCurrent, tNext)) {
        sections.push({
          headingKey: 'tServerInstance',
          current: tCurrent,
          next: tNext
        });
      }
      if (
        effectiveMode !== 'tserver' &&
        effectiveMode !== 'readReplica' &&
        hardwareReviewSectionHasVisibleChanges(mCurrent, mNext)
      ) {
        sections.push({
          headingKey: 'masterServerInstance',
          current: mCurrent,
          next: mNext
        });
      }
      return sections;
    }

    const tCurrent = summaryFromClusterStorage(
      targetCluster.node_spec.instance_type,
      targetCluster.node_spec.storage_spec,
      formatInstanceTypeLabel
    );
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
    if (effectiveMode !== 'master' && hardwareReviewSectionHasVisibleChanges(tCurrent, tNext)) {
      sections.push({
        headingKey: 'tServerInstance',
        current: tCurrent,
        next: tNext
      });
    }
    if (effectiveMode !== 'tserver' && hardwareReviewSectionHasVisibleChanges(mCurrent, mNext)) {
      sections.push({
        headingKey: 'masterServerInstance',
        current: mCurrent,
        next: mNext
      });
    }
    return sections;
  }, [
    targetCluster,
    pendingInstanceSettings,
    formatInstanceTypeLabel,
    useDedicatedNodes,
    isK8s,
    effectiveMode
  ]);

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
    const masterInstanceType = settings.keepMasterTserverSame
      ? tserverInstanceType
      : (settings.masterInstanceType ?? tserverInstanceType);
    const currentMasterStorageSpec =
      targetCluster?.node_spec?.master?.storage_spec ?? currentTserverStorageSpec;
    const masterDeviceInfo = settings.keepMasterTserverSame
      ? settings.deviceInfo
      : (settings.masterDeviceInfo ?? settings.deviceInfo);
    const masterStorageSpec = toResizeStorageSpec(masterDeviceInfo, currentMasterStorageSpec);
    const masterChanged =
      masterInstanceType !== currentMasterInstance ||
      !isEqual(
        toComparableSpecStorage(currentMasterStorageSpec),
        toComparableStorage(masterDeviceInfo)
      );

    const dedicatedNodeSpec: ClusterResizeNodeSpec = {};
    if (effectiveMode !== 'master' && tserverChanged) {
      dedicatedNodeSpec.instance_type = tserverInstanceType ?? undefined;
      dedicatedNodeSpec.storage_spec = tserverStorageSpec;
      dedicatedNodeSpec.tserver = {
        instance_type: tserverInstanceType ?? undefined,
        storage_spec: tserverStorageSpec
      };
    }
    if (effectiveMode !== 'tserver' && (masterChanged || settings.keepMasterTserverSame)) {
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

    if (isK8s) {
      const k8sNodeSpec: ClusterNodeSpec = {
        storage_spec: tserverStorageSpec,
        dedicated_nodes: true
      };

      if (tserverInstanceType) {
        k8sNodeSpec.instance_type = tserverInstanceType;
      }

      const currentTserverK8s = getK8sResourceSpecFromNodeSpec(targetCluster?.node_spec, 'tserver');
      const currentMasterK8s =
        getK8sResourceSpecFromNodeSpec(targetCluster?.node_spec, 'master') ?? currentTserverK8s;
      const nextTserverK8s = settings.tserverK8SNodeResourceSpec ?? currentTserverK8s;
      const nextMasterK8s = settings.keepMasterTserverSame
        ? nextTserverK8s
        : (settings.masterK8SNodeResourceSpec ?? currentMasterK8s ?? nextTserverK8s);

      const tserverK8s = toK8sResourceSpec(nextTserverK8s);
      const masterK8s = toK8sResourceSpec(
        clusterType === ClusterSpecClusterType.ASYNC ? undefined : nextMasterK8s
      );

      if (tserverK8s) {
        k8sNodeSpec.k8s_tserver_resource_spec = tserverK8s;
      }
      if (masterK8s) {
        k8sNodeSpec.k8s_master_resource_spec = masterK8s;
      }

      if (dedicatedNodes) {
        const currentMasterStorageSpec =
          targetCluster?.node_spec?.master?.storage_spec ?? currentTserverStorageSpec;
        const masterDeviceInfo = settings.keepMasterTserverSame
          ? settings.deviceInfo
          : (settings.masterDeviceInfo ?? settings.deviceInfo);
        const masterStorageSpec = toClusterStorageSpec(masterDeviceInfo, currentMasterStorageSpec);
        const masterInstanceType = settings.keepMasterTserverSame
          ? tserverInstanceType
          : (settings.masterInstanceType ?? tserverInstanceType);

        k8sNodeSpec.tserver = {
          ...(tserverInstanceType ? { instance_type: tserverInstanceType } : {}),
          storage_spec: tserverStorageSpec
        };
        if (effectiveMode !== 'readReplica') {
          k8sNodeSpec.master = {
            ...(masterInstanceType ? { instance_type: masterInstanceType } : {}),
            storage_spec: masterStorageSpec
          };
        }
      }

      return k8sNodeSpec;
    }

    if (!dedicatedNodes) {
      return {
        instance_type: tserverInstanceType ?? undefined,
        storage_spec: tserverStorageSpec
      };
    }

    const masterInstanceType = settings.keepMasterTserverSame
      ? tserverInstanceType
      : (settings.masterInstanceType ?? tserverInstanceType);
    const currentMasterStorageSpec =
      targetCluster?.node_spec?.master?.storage_spec ?? currentTserverStorageSpec;
    const masterDeviceInfo = settings.keepMasterTserverSame
      ? settings.deviceInfo
      : (settings.masterDeviceInfo ?? settings.deviceInfo);
    const masterStorageSpec = toClusterStorageSpec(masterDeviceInfo, currentMasterStorageSpec);

    const dedicatedNodeSpec: ClusterNodeSpec = {
      dedicated_nodes: true
    };
    if (tserverInstanceType) {
      dedicatedNodeSpec.instance_type = tserverInstanceType;
    }
    dedicatedNodeSpec.storage_spec = tserverStorageSpec;
    dedicatedNodeSpec.tserver = {
      ...(tserverInstanceType ? { instance_type: tserverInstanceType } : {}),
      storage_spec: tserverStorageSpec
    };
    dedicatedNodeSpec.master = {
      ...(masterInstanceType ? { instance_type: masterInstanceType } : {}),
      storage_spec: masterStorageSpec
    };

    return dedicatedNodeSpec;
  };

  const getExistingClusterNodeCount = () => {
    if (!targetCluster) {
      return undefined;
    }
    if (typeof targetCluster.num_nodes === 'number' && targetCluster.num_nodes >= 1) {
      return targetCluster.num_nodes;
    }
    if (targetCluster.placement_spec) {
      return countRegionsAzsAndNodes(targetCluster.placement_spec).totalNodes;
    }
    if (targetCluster.partitions_spec?.length) {
      return targetCluster.partitions_spec.reduce(
        (sum, partition) =>
          sum + (partition.placement ? countRegionsAzsAndNodes(partition.placement).totalNodes : 0),
        0
      );
    }
    return undefined;
  };

  const submitEditUniverse = (settings: InstanceSettingProps) => {
    if (!universeUUID || !targetCluster?.uuid) {
      toast.error(t('unableToApplyChanges'));
      return;
    }

    const existingNodeCount =
      effectiveMode === 'readReplica' ? getExistingClusterNodeCount() : undefined;

    editUniverse.mutate(
      {
        uniUUID: universeUUID,
        data: {
          expected_universe_version: -1,
          clusters: [
            {
              uuid: targetCluster.uuid,
              ...(existingNodeCount !== undefined ? { num_nodes: existingNodeCount } : {}),
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
    setResizeOptions(undefined);
    setIsLoadingResizeOptions(false);
    setInheritPrimary(false);
    onHide();
  };

  /**
   * K8s hardware edits typically yield UPDATE (not in ResizeUpdateOption), so the API
   * can return []. Force migrate-only → editUniverse. Same fallback on empty/error.
   * Never invent SMART_RESIZE for K8s. While loading (non-K8s), pass undefined so radios stay disabled.
   */
  const effectiveResizeOptions = useMemo((): ResizeUpdateOption[] | undefined => {
    if (isK8s) {
      return [ResizeUpdateOption.FULL_MOVE];
    }
    if (isLoadingResizeOptions) {
      return undefined;
    }
    if (!resizeOptions?.length) {
      return [ResizeUpdateOption.FULL_MOVE];
    }
    return resizeOptions;
  }, [isK8s, isLoadingResizeOptions, resizeOptions]);

  useEffect(() => {
    if (!reviewModalOpen || !pendingInstanceSettings || !universeUUID || !targetCluster?.uuid) {
      return;
    }

    // K8s never supports smart resize in the API mapping; skip the round-trip.
    if (isK8s) {
      setResizeOptions([ResizeUpdateOption.FULL_MOVE]);
      setIsLoadingResizeOptions(false);
      return;
    }

    let cancelled = false;
    setIsLoadingResizeOptions(true);
    setResizeOptions(undefined);

    checkResizeOptions.mutate(
      {
        uniUUID: universeUUID,
        data: {
          cluster_uuid: targetCluster.uuid,
          node_spec: buildEditNodeSpec(pendingInstanceSettings, !!useDedicatedNodes)
        }
      },
      {
        onSuccess: (response) => {
          if (cancelled) return;
          const options = response?.options ?? [];
          setResizeOptions(options.length ? options : [ResizeUpdateOption.FULL_MOVE]);
          setIsLoadingResizeOptions(false);
        },
        onError: (error: unknown) => {
          if (cancelled) return;
          toast.error(createErrorMessage(error));
          setResizeOptions([ResizeUpdateOption.FULL_MOVE]);
          setIsLoadingResizeOptions(false);
        }
      }
    );

    return () => {
      cancelled = true;
    };
    // buildEditNodeSpec closes over latest settings/cluster; re-run when review opens or pending changes.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [
    reviewModalOpen,
    pendingInstanceSettings,
    universeUUID,
    targetCluster?.uuid,
    isK8s,
    useDedicatedNodes
  ]);

  const confirmResizeNodes = ({ delaySeconds, strategy }: ReviewHardwareConfirmPayload) => {
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

    const rollingAllowed = canUseRollingResize(effectiveResizeOptions);
    const migrateAllowed = canUseFullMove(effectiveResizeOptions);

    if (strategy === 'migrate') {
      if (!migrateAllowed) {
        toast.error(t('unableToApplyChanges'));
        return;
      }
      submitEditUniverse(pendingInstanceSettings);
      return;
    }

    if (!rollingAllowed) {
      toast.error(t('unableToApplyChanges'));
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

  const tserverK8sResourceSpec = getK8sResourceSpecFromNodeSpec(
    targetCluster?.node_spec,
    'tserver'
  );
  const masterK8sResourceSpec =
    getK8sResourceSpecFromNodeSpec(targetCluster?.node_spec, 'master') ?? tserverK8sResourceSpec;

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
      volumeSize:
        targetCluster?.node_spec?.master?.storage_spec?.volume_size ?? storageSpec?.volume_size,
      numVolumes:
        targetCluster?.node_spec?.master?.storage_spec?.num_volumes ?? storageSpec?.num_volumes,
      diskIops: targetCluster?.node_spec?.master?.storage_spec?.disk_iops ?? storageSpec?.disk_iops,
      throughput:
        targetCluster?.node_spec?.master?.storage_spec?.throughput ?? storageSpec?.throughput,
      storageClass:
        targetCluster?.node_spec?.master?.storage_spec?.storage_class ?? storageSpec?.storage_class,
      storageType:
        targetCluster?.node_spec?.master?.storage_spec?.storage_type ?? storageSpec?.storage_type,
      mountPoints:
        targetCluster?.node_spec?.master?.storage_spec?.mount_points ?? storageSpec?.mount_points
    },
    tserverK8SNodeResourceSpec: tserverK8sResourceSpec,
    masterK8SNodeResourceSpec: masterK8sResourceSpec
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
    ? (primaryDerivedSettings ?? undefined)
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
      ? countMasterAndTServerNodes(universeData, primaryCluster)
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

  const modalTitle =
    title ??
    (() => {
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
          <TotalNodesBadge
            label={badgeLabel}
            count={badgeCount}
            dataTestId="edit-hardware-total-nodes-badge"
          />
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
              [
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
              ] as unknown as createUniverseFormProps
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
        resizeOptions={effectiveResizeOptions}
        isLoadingOptions={isLoadingResizeOptions}
        replicationFactor={targetCluster.replication_factor}
        onClose={() => setReviewModalOpen(false)}
        onConfirm={confirmResizeNodes}
      />
    </>
  );
};
