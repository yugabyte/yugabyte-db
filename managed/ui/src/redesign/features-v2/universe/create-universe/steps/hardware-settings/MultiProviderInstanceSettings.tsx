import { forwardRef, useContext, useEffect, useImperativeHandle, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import {
  mui,
  yba,
  YBAccordion,
  YBCheckbox,
  YBHelloBanner,
  YBTooltip
} from '@yugabyte-ui-library/core';
import {
  StyledContent,
  StyledEmptyState,
  StyledHeader,
  StyledPanel
} from '../../components/DefaultComponents';
import {
  CreateUniverseContext,
  CreateUniverseContextMethods,
  CreateUniverseSteps,
  StepsRef
} from '../../CreateUniverseContext';
import { getProviderConfigurations } from '../../helpers/providerConfigurations';
import {
  areAllProvidersInstanceConfigured,
  areAllProvidersMasterConfigured,
  areAllProvidersTserverConfigured,
  defaultInstanceSettings,
  stripMasterFromInstanceByProvider
} from '../../helpers/instanceByProvider';
import {
  getMasterCountsByProvider,
  getNodeCountFromByProvider,
  getNodeCountsByProvider
} from '../../helpers/nodesByProvider';
import { getDedicatedTserverMasterCounts } from '../../utils/placementAndAvailability';
import { regionsByProviderFromPlacements } from '../../helpers/placementsByProvider';
import { CloudIconBox } from '../../helpers/CloudIconBox';
import { getNodeCount } from '../../CreateUniverseUtils';
import { ResilienceType } from '../resilence-regions/dtos';
import { ConfigureInstancesModal } from './ConfigureInstancesModal';
import { ProviderInstanceSummaryCard } from './ProviderInstanceSummaryCard';
import { InstanceSettingProps, InstanceSettingsViewMode } from './dtos';
import EditIcon from '@app/redesign/assets/edit-primary.svg';
import AddIcon from '@app/redesign/assets/add2.svg';
import CheckIcon from '@app/redesign/assets/check-green.svg';

const { Box, Typography, Link } = mui;
const { YBButton } = yba;

type ModalState = {
  open: boolean;
  viewMode: Extract<InstanceSettingsViewMode, 'tserverOnly' | 'masterOnly'>;
  startProviderUuid?: string;
};

export const MultiProviderInstanceSettings = forwardRef<StepsRef>((_, forwardRef) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.instanceSettings'
  });
  const { t: tMulti } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.instanceSettings.multiProvider'
  });
  const { t: tCommon } = useTranslation('translation', { keyPrefix: 'common' });

  const [
    {
      generalSettings,
      nodesAvailabilitySettings,
      nodesByProvider,
      instanceByProvider,
      placementsByProvider,
      resilienceAndRegionsSettings
    },
    { moveToNextPage, moveToPreviousPage, saveInstanceByProvider, setActiveStep }
  ] = useContext(CreateUniverseContext) as unknown as CreateUniverseContextMethods;

  const providers = getProviderConfigurations(generalSettings);
  const useDedicatedNodes = !!nodesAvailabilitySettings?.useDedicatedNodes;
  const regionsByProvider = regionsByProviderFromPlacements(placementsByProvider, providers);
  const prevDedicatedRef = useRef(useDedicatedNodes);

  const [bannerOpen, setBannerOpen] = useState(true);
  const [modal, setModal] = useState<ModalState>({
    open: false,
    viewMode: 'tserverOnly'
  });

  useEffect(() => {
    const wasDedicated = prevDedicatedRef.current;
    prevDedicatedRef.current = useDedicatedNodes;
    if (wasDedicated && !useDedicatedNodes && Object.keys(instanceByProvider).length > 0) {
      saveInstanceByProvider(stripMasterFromInstanceByProvider(instanceByProvider));
    }
  }, [useDedicatedNodes, instanceByProvider, saveInstanceByProvider]);

  const tserverReady = areAllProvidersTserverConfigured(providers, instanceByProvider);
  const masterReady = areAllProvidersMasterConfigured(
    providers,
    instanceByProvider,
    useDedicatedNodes
  );
  const allReady = areAllProvidersInstanceConfigured(
    providers,
    instanceByProvider,
    useDedicatedNodes
  );

  const azByProvider = Object.fromEntries(
    Object.entries(nodesByProvider).map(([uuid, slice]) => [uuid, slice.availabilityZones])
  );
  const totalNodes =
    Object.keys(nodesByProvider).length > 0
      ? getNodeCountFromByProvider(azByProvider)
      : getNodeCount(nodesAvailabilitySettings?.availabilityZones ?? {});
  const toBreakdown = (counts: Record<string, number>) =>
    providers
      .map((provider) => ({
        uuid: provider.uuid,
        code: provider.code,
        count: counts[provider.uuid] ?? 0
      }))
      .filter((entry) => entry.count > 0);
  const tserverBreakdown = toBreakdown(getNodeCountsByProvider(azByProvider));
  const masterTotal =
    getDedicatedTserverMasterCounts(
      resilienceAndRegionsSettings,
      nodesAvailabilitySettings,
      nodesByProvider
    )?.master ?? 0;
  const masterBreakdown = toBreakdown(
    getMasterCountsByProvider(azByProvider, providers, masterTotal)
  );

  const keepMasterSame = providers.every(
    (provider) => instanceByProvider[provider.uuid]?.keepMasterTserverSame ?? true
  );
  const setKeepMasterSame = (checked: boolean) =>
    saveInstanceByProvider(
      Object.fromEntries(
        providers.map((provider) => [
          provider.uuid,
          {
            ...defaultInstanceSettings(),
            ...instanceByProvider[provider.uuid],
            keepMasterTserverSame: checked
          }
        ])
      )
    );

  useImperativeHandle(
    forwardRef,
    () => ({
      onNext: async () => {
        if (!allReady) return false;
        moveToNextPage();
        return true;
      },
      onPrev: () => {
        if (resilienceAndRegionsSettings?.resilienceType === ResilienceType.SINGLE_NODE) {
          setActiveStep(CreateUniverseSteps.RESILIENCE_AND_REGIONS);
        } else {
          moveToPreviousPage();
        }
      }
    }),
    [
      allReady,
      moveToNextPage,
      moveToPreviousPage,
      setActiveStep,
      resilienceAndRegionsSettings?.resilienceType
    ]
  );

  const openModal = (
    viewMode: Extract<InstanceSettingsViewMode, 'tserverOnly' | 'masterOnly'>,
    startProviderUuid?: string
  ) => setModal({ open: true, viewMode, startProviderUuid });

  const renderNodeBadge = (
    label: string,
    testId: string,
    total: number,
    providerBreakdown: ReturnType<typeof toBreakdown>,
    showEditLink: boolean
  ) => (
    <Box
      data-testid={testId}
      sx={{
        display: 'inline-flex',
        alignItems: 'center',
        border: '1px solid #D7DEE4',
        borderRadius: '8px',
        px: '16px',
        py: '10px',
        gap: '16px',
        width: 'fit-content',
        backgroundColor: '#fff'
      }}
    >
      <Typography
        sx={{
          fontSize: '13px',
          fontWeight: 500,
          lineHeight: '20px',
          letterSpacing: '0.2px',
          color: '#0B1117',
          whiteSpace: 'pre'
        }}
      >
        {`${label}  ${total}`}
      </Typography>
      {providerBreakdown.length > 0 && (
        <>
          <Box sx={{ width: '1px', height: '16px', backgroundColor: '#D7DEE4' }} aria-hidden />
          <Box sx={{ display: 'inline-flex', alignItems: 'center', gap: '16px' }}>
            {providerBreakdown.map((entry, index) => (
              <Box
                key={entry.uuid}
                sx={{ display: 'inline-flex', alignItems: 'center', gap: '16px' }}
              >
                {index > 0 && (
                  <Typography sx={{ fontSize: '11.5px', fontWeight: 500, color: '#6D7C88' }}>
                    +
                  </Typography>
                )}
                <Box sx={{ display: 'inline-flex', alignItems: 'center', gap: '8px' }}>
                  <CloudIconBox code={entry.code} size={16} />
                  <Typography sx={{ fontSize: '13px', fontWeight: 500, color: '#0B1117' }}>
                    {entry.count}
                  </Typography>
                </Box>
              </Box>
            ))}
          </Box>
          {showEditLink && (
            <Box sx={{ width: '1px', height: '16px', backgroundColor: '#D7DEE4' }} aria-hidden />
          )}
        </>
      )}
      {showEditLink && (
        <Link
          component="button"
          type="button"
          underline="always"
          onClick={() => setActiveStep(CreateUniverseSteps.NODES_AVAILABILITY)}
          sx={{
            fontSize: '11.5px',
            fontWeight: 600,
            lineHeight: '16px',
            color: '#2B59C3',
            cursor: 'pointer'
          }}
        >
          {tCommon('edit')}
        </Link>
      )}
    </Box>
  );

  const renderEmptyState = (
    viewMode: Extract<InstanceSettingsViewMode, 'tserverOnly' | 'masterOnly'>,
    copy: string,
    buttonLabel: string,
    disabledTooltip?: string
  ) => (
    <StyledEmptyState data-testid={`instance-empty-${viewMode}`} sx={{ gap: '16px' }}>
      <Typography
        sx={{
          fontSize: '13px',
          fontWeight: 400,
          lineHeight: '19px',
          color: '#4E5F6D',
          textAlign: 'center',
          width: '100%'
        }}
      >
        {copy}
      </Typography>
      <YBTooltip title={disabledTooltip ?? ''}>
        {/* span keeps the tooltip working on a disabled button */}
        <span>
          <YBButton
            variant="secondary"
            size="medium"
            startIcon={<AddIcon />}
            disabled={!!disabledTooltip}
            onClick={() => openModal(viewMode)}
            dataTestId={`configure-instance-${viewMode}`}
          >
            {buttonLabel}
          </YBButton>
        </span>
      </YBTooltip>
    </StyledEmptyState>
  );

  const renderEditButton = (
    viewMode: Extract<InstanceSettingsViewMode, 'tserverOnly' | 'masterOnly'>
  ) => (
    <YBButton
      variant="ghost"
      size="medium"
      startIcon={<EditIcon />}
      onClick={() => openModal(viewMode)}
      dataTestId={`edit-instance-${viewMode}`}
    >
      {tCommon('edit')}
    </YBButton>
  );

  const renderConfiguredCards = (
    viewMode: Extract<InstanceSettingsViewMode, 'tserverOnly' | 'masterOnly'>,
    showMaster: boolean
  ) => (
    <>
      <Box sx={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
        <Box component="span" sx={{ display: 'inline-flex', width: 24, height: 24 }}>
          <CheckIcon />
        </Box>
        <Typography
          sx={{ fontSize: '13px', fontWeight: 500, lineHeight: '16px', color: '#097345' }}
        >
          {tMulti('aligned')}
        </Typography>
      </Box>
      <Box sx={{ display: 'flex', flexDirection: 'column', gap: '24px', width: '100%' }}>
        {providers.map((provider) => {
          const settings = instanceByProvider[provider.uuid];
          if (!settings) return null;
          return (
            <Box
              key={provider.uuid}
              onClick={() => openModal(viewMode, provider.uuid)}
              sx={{ cursor: 'pointer' }}
            >
              <ProviderInstanceSummaryCard
                provider={provider}
                settings={settings}
                showMaster={showMaster}
              />
            </Box>
          );
        })}
      </Box>
    </>
  );

  const renderMasterSection = () => (
    <YBAccordion
      defaultExpanded
      titleContent={t('master')}
      sx={{ width: '100%' }}
      dataTestId="multi-instance-master-section"
      renderChipsWithoutExpanded={
        !keepMasterSame && masterReady
          ? () => (
              // Header click toggles the accordion; keep Edit from collapsing it.
              <Box onClick={(e) => e.stopPropagation()}>{renderEditButton('masterOnly')}</Box>
            )
          : undefined
      }
    >
      <Box sx={{ display: 'flex', flexDirection: 'column', gap: '24px' }}>
        {renderNodeBadge(
          t('totalMasterServerNodes'),
          'multi-instance-total-master-nodes',
          masterTotal,
          masterBreakdown,
          false
        )}
        <YBCheckbox
          label={t('keepMasterTserverSame')}
          checked={keepMasterSame}
          onChange={(_, checked) => setKeepMasterSame(checked)}
          size="large"
          dataTestId="multi-keep-master-tserver-same"
        />
        {!keepMasterSame &&
          (masterReady
            ? renderConfiguredCards('masterOnly', true)
            : tserverReady
              ? renderEmptyState(
                  'masterOnly',
                  tMulti('configureMasterReady'),
                  tMulti('configureMasterInstance')
                )
              : renderEmptyState(
                  'masterOnly',
                  tMulti('configureMasterEmpty'),
                  tMulti('configureMasterInstance'),
                  tMulti('configureTserverFirst')
                ))}
      </Box>
    </YBAccordion>
  );

  const renderTserverSection = () => (
    <StyledPanel>
      <Box
        sx={{
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'space-between',
          height: '64px',
          px: '24px'
        }}
      >
        <StyledHeader sx={{ p: 0 }}>
          {useDedicatedNodes ? t('tserver') : t('clusterInstance')}
        </StyledHeader>
        {tserverReady && renderEditButton('tserverOnly')}
      </Box>
      <StyledContent sx={{ gap: '24px' }}>
        {renderNodeBadge(
          useDedicatedNodes ? t('totalTServerNodes') : t('totalNodes'),
          'multi-instance-total-tserver-nodes',
          totalNodes,
          tserverBreakdown,
          true
        )}
        {!tserverReady && (
          <YBHelloBanner
            title={tMulti('capacityBannerTitle')}
            subTitle={tMulti('capacityBannerBody')}
            showBanner={bannerOpen}
            setShowBanner={setBannerOpen}
          />
        )}
        {tserverReady
          ? renderConfiguredCards('tserverOnly', false)
          : renderEmptyState('tserverOnly', tMulti('configureEmpty'), tMulti('configureInstance'))}
      </StyledContent>
    </StyledPanel>
  );

  return (
    <Box
      sx={{ display: 'flex', flexDirection: 'column', gap: 3 }}
      data-testid="multi-provider-instance-settings"
    >
      {renderTserverSection()}
      {useDedicatedNodes && renderMasterSection()}

      <ConfigureInstancesModal
        open={modal.open}
        providers={providers}
        regionsByProvider={regionsByProvider}
        initialByProvider={instanceByProvider}
        viewMode={modal.viewMode}
        startProviderUuid={modal.startProviderUuid}
        onClose={() => setModal((prev) => ({ ...prev, open: false }))}
        onCommit={(draft: Record<string, InstanceSettingProps>) => saveInstanceByProvider(draft)}
      />
    </Box>
  );
});

MultiProviderInstanceSettings.displayName = 'MultiProviderInstanceSettings';
