import { forwardRef, useContext, useEffect, useImperativeHandle } from 'react';
import { FormProvider, useForm } from 'react-hook-form';
import { yupResolver } from '@hookform/resolvers/yup';
import { useQuery } from 'react-query';
import { Trans, useTranslation } from 'react-i18next';
import { mui, YBAccordion, YBCheckboxField } from '@yugabyte-ui-library/core';
import {
  StyledContent,
  StyledHeader,
  StyledPanel
} from '@app/redesign/features-v2/universe/create-universe/components/DefaultComponents';
import { TotalNodesBadge } from '@app/redesign/features-v2/universe/create-universe/components/TotalNodesBadge';
import {
  CPUArchField,
  LinuxVersionField,
  SpotInstanceField,
  InstanceTypeField,
  VolumeInfoField,
  StorageTypeField,
  K8NodeSpecField,
  K8VolumeInfoField,
  EBSVolumeField,
  EBSKmsConfigField
} from '@app/redesign/features-v2/universe/create-universe/fields';
import { isImgBundleSupportedByProvider } from '@app/components/configRedesign/providerRedesign/components/linuxVersionCatalog/LinuxVersionUtils';
import { useRuntimeConfigValues } from '@app/redesign/features-v2/universe/create-universe/helpers/utils';
import { usePersistStepFormValues } from '@app/redesign/features-v2/universe/create-universe/helpers/persistStepFormValues';
import { QUERY_KEY, api } from '@app/redesign/features/universe/universe-form/utils/api';
import { useGetZones } from '@app/redesign/features-v2/universe/create-universe/fields/instance-type/InstanceTypeFieldHelper';
import {
  CloudType,
  Placement,
  Region
} from '@app/redesign/features/universe/universe-form/utils/dto';
import { isSpotInstanceCloudType } from '@app/components/configRedesign/providerRedesign/utils';
import {
  CreateUniverseContext,
  CreateUniverseContextMethods,
  CreateUniverseSteps,
  StepsRef
} from '@app/redesign/features-v2/universe/create-universe/CreateUniverseContext';
import { ResilienceType } from '@app/redesign/features-v2/universe/create-universe/steps/resilence-regions/dtos';
import {
  InstanceSettingProps,
  InstanceSettingsViewMode
} from '@app/redesign/features-v2/universe/create-universe/steps/hardware-settings/dtos';
import { InstanceSettingsValidationSchema } from '@app/redesign/features-v2/universe/create-universe/steps/hardware-settings/ValidationSchema';
import { ArchitectureType } from '@app/redesign/features-v2/universe/create-universe/helpers/constants';
import {
  getDedicatedTserverMasterCounts,
  getNodeCount
} from '@app/redesign/features-v2/universe/create-universe/CreateUniverseUtils';
import {
  DEVICE_INFO_FIELD,
  INSTANCE_TYPE_FIELD,
  LINUX_VERSION_FIELD,
  MASTER_DEVICE_INFO_FIELD,
  MASTER_INSTANCE_TYPE_FIELD,
  MASTER_K8_NODE_SPEC_FIELD,
  TSERVER_K8_NODE_SPEC_FIELD,
  MASTER_TSERVER_SAME_FIELD,
  ENABLE_EBS_CONFIG_FIELD,
  EBS_KMS_CONFIG_FIELD,
  CPU_ARCH_FIELD
} from '@app/redesign/features-v2/universe/create-universe/fields/FieldNames';
import { getPrimaryProvider } from '../../helpers/providerConfigurations';
import { getEffectiveRegions } from '../../helpers/placementsByProvider';
import { ProviderType } from '../general-settings/dtos';

const { Box, CircularProgress, Typography } = mui;

export const InstanceBox = ({ children }: { children: React.ReactNode }) => (
  <Box sx={{ width: 480, display: 'flex', flexDirection: 'column', gap: 2 }}>{children}</Box>
);

const PanelWrapper = ({
  children,
  editMode
}: {
  children: React.ReactNode;
  editMode?: boolean;
}) => (
  <Box
    sx={(theme) => ({
      display: 'flex',
      width: editMode ? 'auto' : '734px',
      flexDirection: 'column',
      backgroundColor: '#FBFCFD',
      border: `1px solid ${theme.palette.grey[300]}`,
      borderRadius: '8px',
      padding: '24px'
    })}
  >
    {children}
  </Box>
);

export type { InstanceSettingsViewMode };

export type InstanceSettingsProps = {
  editMode?: boolean;
  /**
   * Controls which subsections of the hardware form are rendered.
   * - `all` (default): T-Server section, plus Master accordion when dedicated nodes are enabled.
   * - `tserverOnly`: T-Server section only (no Master accordion). Used for the dedicated
   *   T-Server edit modal where master is edited via a separate modal.
   * - `masterOnly`: Master section only, rendered as a flat panel without the accordion
   *   wrapper. Used for the dedicated Master edit modal. The "Keep master same as T-Server"
   *   checkbox is rendered above the panel instead of inside an accordion.
   */
  viewMode?: InstanceSettingsViewMode;
  /**
   * When true, all hardware input fields in the T-Server (or non-dedicated cluster) section
   * are rendered as disabled. Used by the read-replica edit flow to lock fields while the
   * "Keep read replica instance settings same as primary cluster" checkbox is checked.
   */
  disableTserverFields?: boolean;
  /** Multi-provider modal: bind fields to this provider instead of primary. */
  providerOverride?: ProviderType;
  /** Multi-provider modal: regions for instance-type queries. */
  regionsOverride?: Region[];
  /** Embedded form defaults (modal draft). */
  formDefaults?: InstanceSettingProps;
  /** Embedded: called after successful validate instead of advancing the wizard. */
  onValidated?: (data: InstanceSettingProps) => void;
  /** Skip step chrome + context persistence (Configure Instances modal). */
  embedded?: boolean;
};

export const InstanceSettingsForm = forwardRef<StepsRef, InstanceSettingsProps>(
  (
    {
      editMode = false,
      viewMode = 'all',
      disableTserverFields = false,
      providerOverride,
      regionsOverride,
      formDefaults,
      onValidated,
      embedded = false
    },
    forwardRef
  ) => {
  const [
    {
      instanceSettings,
      generalSettings,
      nodesAvailabilitySettings,
      resilienceAndRegionsSettings,
      placementsByProvider,
      securitySettings
    },
    { moveToNextPage, moveToPreviousPage, saveInstanceSettings, setActiveStep }
  ] = useContext(CreateUniverseContext) as unknown as CreateUniverseContextMethods;

  const provider = providerOverride ?? getPrimaryProvider(generalSettings);
  const effectiveRegions =
    regionsOverride ?? getEffectiveRegions(resilienceAndRegionsSettings, placementsByProvider);
  const isK8s = provider?.code === CloudType.kubernetes;
  const isOnPrem = provider?.code === CloudType.onprem;
  const useDedicatedNodes = nodesAvailabilitySettings?.useDedicatedNodes;

  //Runtime configs
  const {
    osPatchingEnabled,
    useK8CustomResources,
    maxVolumeCount,
    canUseSpotInstance,
    isRuntimeConfigLoading,
    isProviderRuntimeConfigLoading,
    ebsVolumeEnabled,
    enableAzOverridesK8s
  } = useRuntimeConfigValues(provider?.uuid);

  const k8sOverrideEnabled = !!editMode && enableAzOverridesK8s;
  //Runtime configs

  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.instanceSettings'
  });

  const earKMSConfig = securitySettings?.kmsConfig ?? null;

  const methods = useForm<InstanceSettingProps>({
    defaultValues: formDefaults ?? instanceSettings,
    mode: 'onSubmit',
    reValidateMode: 'onChange',
    context: {
      earKmsConfig: earKMSConfig
    },
    resolver: yupResolver(
      InstanceSettingsValidationSchema(
        t,
        useK8CustomResources,
        provider?.code,
        !!useDedicatedNodes,
        maxVolumeCount
      )
    )
  });
  usePersistStepFormValues(
    methods.watch,
    methods.getValues,
    embedded ? () => undefined : saveInstanceSettings
  );
  const { watch, setValue, control, trigger } = methods;
  const {
    formState: { isSubmitted }
  } = methods;

  const deviceInfo = watch(DEVICE_INFO_FIELD);
  const sameAsTserver = watch(MASTER_TSERVER_SAME_FIELD);
  const instanceType = watch(INSTANCE_TYPE_FIELD);
  const nodeSpec = watch(TSERVER_K8_NODE_SPEC_FIELD);
  const ebsEnabled = watch(ENABLE_EBS_CONFIG_FIELD);
  const cpuArch = watch(CPU_ARCH_FIELD);

  const needsInstanceTypes = !isK8s || !useK8CustomResources;
  const { zones, isLoadingZones } = useGetZones(provider, effectiveRegions);
  const zoneNames = zones.map((zone: Placement) => zone.name);
  const hasRegionsForZones = effectiveRegions.length > 0;
  const instanceTypesQueryEnabled =
    needsInstanceTypes && !!provider?.uuid && zoneNames.length > 0 && !isLoadingZones;

  const { isFetched: isInstanceTypesFetched, isError: isInstanceTypesError } = useQuery(
    [
      QUERY_KEY.getInstanceTypes,
      provider?.uuid,
      JSON.stringify(zoneNames),
      osPatchingEnabled ? cpuArch : null
    ],
    () => api.getInstanceTypes(provider?.uuid, zoneNames, osPatchingEnabled ? cpuArch : null),
    {
      enabled: instanceTypesQueryEnabled,
      refetchOnWindowFocus: false
    }
  );

  // Don't paint the form until zones + the full instance-type list are ready.
  // A disabled query is not "loading" in react-query v3, so isLoading alone
  // lets the dropdown render empty and then jump when the list arrives.
  const showInstanceTypesLoader =
    needsInstanceTypes &&
    !!provider?.uuid &&
    hasRegionsForZones &&
    (isLoadingZones ||
      zoneNames.length === 0 ||
      (instanceTypesQueryEnabled && !isInstanceTypesFetched && !isInstanceTypesError));

  useEffect(() => {
    if (osPatchingEnabled && provider && !isImgBundleSupportedByProvider(provider)) {
      setValue(LINUX_VERSION_FIELD, null);
    }
  }, [provider?.uuid]);

  useEffect(() => {
    if (earKMSConfig && ebsEnabled) {
      trigger(EBS_KMS_CONFIG_FIELD);
    }
  }, [earKMSConfig, ebsEnabled, trigger]);

  useEffect(() => {
    if (deviceInfo && sameAsTserver) {
      setValue(MASTER_DEVICE_INFO_FIELD, deviceInfo, {
        shouldValidate: isSubmitted,
        shouldDirty: true
      });
      //instance type not present for k8s
      if (instanceType) {
        setValue(MASTER_INSTANCE_TYPE_FIELD, instanceType, {
          shouldValidate: isSubmitted,
          shouldDirty: true
        });
      }
      //node spec for k8s
      if (nodeSpec) {
        setValue(MASTER_K8_NODE_SPEC_FIELD, nodeSpec, {
          shouldValidate: isSubmitted,
          shouldDirty: true
        });
      }
    }
  }, [deviceInfo, sameAsTserver, instanceType, nodeSpec, isSubmitted, setValue]);

  // revalidate master when toggle changes
  useEffect(() => {
    if (!isSubmitted) return;
    void trigger([MASTER_DEVICE_INFO_FIELD, MASTER_INSTANCE_TYPE_FIELD, MASTER_K8_NODE_SPEC_FIELD]);
  }, [sameAsTserver, isSubmitted, trigger]);

  useImperativeHandle(
    forwardRef,
    () => ({
      onNext: () =>
        methods.handleSubmit((data) => {
          if (embedded) {
            onValidated?.(data);
            return;
          }
          moveToNextPage();
        })(),
      onPrev: () => {
        if (embedded) return;
        if (resilienceAndRegionsSettings?.resilienceType === ResilienceType.SINGLE_NODE) {
          setActiveStep(CreateUniverseSteps.RESILIENCE_AND_REGIONS);
        } else {
          moveToPreviousPage();
        }
      }
    }),
    [embedded, onValidated, moveToNextPage, moveToPreviousPage, setActiveStep, resilienceAndRegionsSettings?.resilienceType, methods]
  );

  const showDedicatedNodesSection = !!(
    (useDedicatedNodes ?? false) ||
    (useK8CustomResources && isK8s)
  );
  const isMasterOnlyDedicatedView = viewMode === 'masterOnly' && showDedicatedNodesSection;
  const isTServerOnlyView = viewMode === 'tserverOnly';

  const dedicatedCounts = getDedicatedTserverMasterCounts(
    resilienceAndRegionsSettings,
    nodesAvailabilitySettings
  );
  const tserverNodeCount =
    dedicatedCounts?.tserver ??
    (resilienceAndRegionsSettings?.resilienceType === ResilienceType.SINGLE_NODE
      ? 1
      : getNodeCount(nodesAvailabilitySettings?.availabilityZones ?? {}));
  const masterNodeCount = dedicatedCounts?.master ?? 0;
  const goToNodesAvailibility = () => setActiveStep(CreateUniverseSteps.NODES_AVAILABILITY);

  // this file is also used in edit universe hardware tab. To match the design there we need to conditionally change Panel and Content components
  const Panel = editMode ? Box : StyledPanel;
  const Content = editMode ? Box : StyledContent;

  if (isRuntimeConfigLoading || isProviderRuntimeConfigLoading || showInstanceTypesLoader) {
    if (embedded) {
      return (
        <Box display="flex" alignItems="center" justifyContent="center" width="100%" py={4}>
          <CircularProgress />
        </Box>
      );
    }
    return (
      <Panel>
        {!editMode && <StyledHeader />}
        <Content>
          <PanelWrapper editMode={editMode}>
            <Box display="flex" alignItems="center" justifyContent="center" width="100%">
              <CircularProgress />
            </Box>
          </PanelWrapper>
        </Content>
      </Panel>
    );
  }

  const tserverFormFields = (
    <InstanceBox>
      {osPatchingEnabled &&
        provider &&
        (isImgBundleSupportedByProvider(provider) || provider.code === CloudType.onprem) &&
        !editMode && (
          <>
            <CPUArchField
              supportedArchs={
                provider.code === CloudType.onprem
                  ? [ArchitectureType.X86_64, ArchitectureType.ARM64]
                  : (provider.imageBundles?.map((img) => img.details.arch) ?? [])
              }
              disabled={false}
            />
            {provider.code !== CloudType.onprem && (
              <LinuxVersionField disabled={false} provider={provider} />
            )}
          </>
        )}
      {provider && isSpotInstanceCloudType(provider.code) && canUseSpotInstance && (
        <SpotInstanceField disabled={false} cloudType={provider.code} />
      )}
      {!isK8s && (
        <>
          <InstanceTypeField
            isEditMode={editMode}
            isMaster={false}
            disabled={disableTserverFields}
            provider={provider}
            regions={effectiveRegions}
          />
          <VolumeInfoField
            isEditMode={editMode}
            isMaster={false}
            maxVolumeCount={maxVolumeCount}
            disabled={disableTserverFields}
            provider={provider}
            useDedicatedNodes={useDedicatedNodes}
            regions={effectiveRegions}
          />
        </>
      )}
      {isK8s &&
        (useK8CustomResources ? (
          <>
            <K8NodeSpecField
              isMaster={false}
              disabled={disableTserverFields}
              provider={provider}
            />
            <K8VolumeInfoField
              isMaster={false}
              maxVolumeCount={maxVolumeCount}
              disableVolumeSize={false}
              disabled={disableTserverFields}
              provider={provider}
              isEditMode={editMode}
              k8sOverrideEnabled={k8sOverrideEnabled}
            />
          </>
        ) : (
          <>
            <InstanceTypeField
              isEditMode={editMode}
              isMaster={false}
              disabled={disableTserverFields}
              provider={provider}
              regions={effectiveRegions}
            />
            <VolumeInfoField
              isEditMode={editMode}
              isMaster={false}
              maxVolumeCount={maxVolumeCount}
              disabled={disableTserverFields}
              provider={provider}
              useDedicatedNodes={useDedicatedNodes}
              regions={effectiveRegions}
            />
          </>
        ))}
      {deviceInfo && provider?.code === CloudType.gcp && useDedicatedNodes && (
        <StorageTypeField
          disabled={disableTserverFields}
          isEditMode={editMode}
          provider={provider}
        />
      )}
      {ebsVolumeEnabled && provider?.code === CloudType.aws && (
        <EBSVolumeField disabled={disableTserverFields || editMode} />
      )}
      {ebsVolumeEnabled && provider?.code === CloudType.aws && ebsEnabled && (
        <EBSKmsConfigField disabled={disableTserverFields || editMode} />
      )}
    </InstanceBox>
  );

  const masterNodesBadge =
    !embedded && !editMode && useDedicatedNodes ? (
      <Box sx={{ mb: 3 }}>
        <TotalNodesBadge
          label={t(isK8s ? 'totalMasterServerPods' : 'totalMasterServerNodes')}
          count={masterNodeCount}
          onEdit={goToNodesAvailibility}
          dataTestId="instance-settings-total-master-nodes"
        />
      </Box>
    ) : null;

  const sameCheckbox = (
    <Box mb={2}>
      <YBCheckboxField
        label={t('keepMasterTserverSame')}
        control={control}
        name={MASTER_TSERVER_SAME_FIELD}
        size="large"
        dataTestId="keep-master-tserver-same-field"
      />
    </Box>
  );

  const masterFormPanel = (
    <PanelWrapper editMode={editMode || embedded}>
      <InstanceBox>
        {!isK8s && useDedicatedNodes && (
          <>
            <InstanceTypeField
              isEditMode={editMode}
              isMaster={true}
              disabled={!!sameAsTserver}
              provider={provider}
              regions={effectiveRegions}
            />
            <VolumeInfoField
              isEditMode={editMode}
              isMaster={true}
              maxVolumeCount={maxVolumeCount}
              disabled={!!sameAsTserver}
              provider={provider}
              useDedicatedNodes={useDedicatedNodes}
              regions={effectiveRegions}
            />
          </>
        )}
        {isK8s && useK8CustomResources && (
          <>
            <K8NodeSpecField isMaster={true} disabled={!!sameAsTserver} provider={provider} />
            <K8VolumeInfoField
              isMaster={true}
              disableVolumeSize={false}
              maxVolumeCount={maxVolumeCount}
              disabled={!!sameAsTserver}
              provider={provider}
              isEditMode={editMode}
              k8sOverrideEnabled={k8sOverrideEnabled}
            />
          </>
        )}
      </InstanceBox>
    </PanelWrapper>
  );

  // Multi-provider modal: keep-same checkbox lives on the page, so embedded shows the global-settings note instead.
  const masterGlobalNote = (
    <Typography sx={{ mt: 2, fontSize: '11.5px', lineHeight: '16px', color: '#4E5F6D' }}>
      <Trans
        t={t}
        i18nKey={
          provider?.code === CloudType.aws ? 'multiProvider.masterNoteAws' : 'multiProvider.masterNote'
        }
        components={{ b: <b /> }}
      />
    </Typography>
  );

  const masterSection =
    showDedicatedNodesSection && !isTServerOnlyView
      ? isMasterOnlyDedicatedView || embedded
        ? (
            <Box>
              {masterNodesBadge}
              {embedded ? null : sameCheckbox}
              {masterFormPanel}
              {embedded && isMasterOnlyDedicatedView && masterGlobalNote}
            </Box>
          )
        : (
            <YBAccordion defaultExpanded titleContent={<>{t('master')}</>} sx={{ width: '100%' }}>
              <Box>
                {masterNodesBadge}
                {sameCheckbox}
                {masterFormPanel}
              </Box>
            </YBAccordion>
          )
      : null;

  if (embedded) {
    return (
      <FormProvider {...methods}>
        {!isMasterOnlyDedicatedView && (
          <PanelWrapper editMode>{tserverFormFields}</PanelWrapper>
        )}
        {masterSection}
      </FormProvider>
    );
  }

  return (
    <FormProvider {...methods}>
      {!isMasterOnlyDedicatedView && (
        <Panel>
          {!editMode && (
            <Box>
              <StyledHeader>
                {showDedicatedNodesSection ? t('tserver') : t('clusterInstance')}
              </StyledHeader>
              <Box sx={{ px: '24px', pb: '16px' }}>
                <TotalNodesBadge
                  label={
                    useDedicatedNodes
                      ? t(isK8s ? 'totalTServerPods' : 'totalTServerNodes')
                      : t(isK8s ? 'totalPods' : 'totalNodes')
                  }
                  count={tserverNodeCount}
                  onEdit={goToNodesAvailibility}
                  dataTestId={
                    useDedicatedNodes
                      ? 'instance-settings-total-tserver-nodes'
                      : 'instance-settings-total-nodes'
                  }
                />
              </Box>
            </Box>
          )}

          <Content>
            <PanelWrapper editMode={editMode}>{tserverFormFields}</PanelWrapper>
          </Content>
        </Panel>
      )}
      {masterSection}
    </FormProvider>
  );
});

InstanceSettingsForm.displayName = 'InstanceSettingsForm';

/** @deprecated Prefer {@link InstanceSettingsForm}; kept for existing edit-universe imports. */
export const InstanceSettings = InstanceSettingsForm;