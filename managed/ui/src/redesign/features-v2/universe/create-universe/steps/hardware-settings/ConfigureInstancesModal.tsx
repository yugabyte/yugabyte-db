import { useEffect, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { mui, yba } from '@yugabyte-ui-library/core';
import { StepsRef } from '../../CreateUniverseContext';
import { ProviderType } from '../general-settings/dtos';
import { InstanceSettingProps } from './dtos';
import { InstanceSettingsForm } from './InstanceSettings';
import { InstanceSettingsViewMode } from './dtos';
import {
  defaultInstanceSettings,
  isMasterInstanceConfigured,
  isTserverInstanceConfigured
} from '../../helpers/instanceByProvider';
import { CloudIconBox } from '../../helpers/CloudIconBox';
import { Region } from '@app/redesign/features/universe/universe-form/utils/dto';
import CheckWhiteIcon from '@app/redesign/assets/check-white.svg';

const { YBModal } = yba;
const { Box, Typography } = mui;

const providerLabelSx = {
  fontSize: '13px',
  fontWeight: 400,
  lineHeight: '16px',
  letterSpacing: '-0.2px'
} as const;

type Props = {
  open: boolean;
  providers: ProviderType[];
  regionsByProvider: Record<string, Region[]>;
  /** Committed values used to seed the draft when the modal opens. */
  initialByProvider: Record<string, InstanceSettingProps>;
  viewMode: Extract<InstanceSettingsViewMode, 'tserverOnly' | 'masterOnly'>;
  startProviderUuid?: string;
  onClose: () => void;
  onCommit: (draft: Record<string, InstanceSettingProps>) => void;
};

export function ConfigureInstancesModal({
  open,
  providers,
  regionsByProvider,
  initialByProvider,
  viewMode,
  startProviderUuid,
  onClose,
  onCommit
}: Props) {
  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.instanceSettings.multiProvider'
  });
  const { t: tCommon } = useTranslation('translation', { keyPrefix: 'common' });
  const formRef = useRef<StepsRef>(null);
  const [draft, setDraft] = useState<Record<string, InstanceSettingProps>>({});
  // The form reads formDefaults only on mount, so it must not mount before the draft is seeded.
  const [seeded, setSeeded] = useState(false);
  const [stepIndex, setStepIndex] = useState(0);

  // Seed draft only when the modal opens so parent re-renders don't wipe in-progress edits.
  useEffect(() => {
    if (!open) {
      setSeeded(false);
      return;
    }
    const initialDraft: Record<string, InstanceSettingProps> = {};
    for (const provider of providers) {
      initialDraft[provider.uuid] = {
        ...defaultInstanceSettings(),
        ...initialByProvider[provider.uuid]
      };
    }
    setDraft(initialDraft);
    const startIdx = providers.findIndex((provider) => provider.uuid === startProviderUuid);
    setStepIndex(startIdx >= 0 ? startIdx : 0);
    setSeeded(true);
    // eslint-disable-next-line react-hooks/exhaustive-deps -- seed on open only
  }, [open]);

  const current = providers[stepIndex];
  const isLast = stepIndex >= providers.length - 1;

  const handleValidated = (data: InstanceSettingProps) => {
    if (!current) return;
    const nextDraft = { ...draft, [current.uuid]: data };
    setDraft(nextDraft);
    if (isLast) {
      onCommit(nextDraft);
      onClose();
      return;
    }
    setStepIndex((idx) => idx + 1);
  };

  const handlePrimary = () => {
    void formRef.current?.onNext();
  };

  if (!current) return null;

  return (
    <YBModal
      open={open}
      onClose={onClose}
      // shortcut: no Back button between provider steps (YBModal has no secondary-action slot), add one if users need to revisit a provider.
      title={t(viewMode === 'masterOnly' ? 'masterModalTitle' : 'tserverModalTitle')}
      titleSeparator
      overrideWidth={800}
      overrideHeight="auto"
      submitTestId="configure-instances-modal-next"
      cancelTestId="configure-instances-modal-cancel"
      onSubmit={handlePrimary}
      submitLabel={isLast ? tCommon('save') : tCommon('next')}
      cancelLabel={tCommon('cancel')}
      buttonProps={{
        primary: { dataTestId: 'configure-instances-modal-next' }
      }}
    >
      <Box sx={{ display: 'flex', flexDirection: 'column', gap: 2, pb: 1 }}>
        <Box
          sx={{
            display: 'flex',
            alignItems: 'center',
            gap: '14px',
            flexWrap: 'wrap',
            px: '8px',
            py: '8px',
            borderBottom: '1px solid #E5E5E9',
            mb: 1
          }}
          data-testid="configure-instances-stepper"
        >
          {providers.map((provider, index) => {
            const active = index === stepIndex;
            const codeLabel = (provider.code ?? '').toUpperCase();
            const settings = draft[provider.uuid];
            const configured =
              viewMode === 'masterOnly'
                ? isMasterInstanceConfigured(settings, true, provider.code)
                : isTserverInstanceConfigured(settings, provider.code);
            return (
              <Box
                key={provider.uuid}
                sx={{ display: 'inline-flex', alignItems: 'center', gap: '14px' }}
              >
                {index > 0 && (
                  <Typography
                    aria-hidden
                    sx={{
                      fontSize: '16px',
                      lineHeight: '24px',
                      color: '#97A5B0',
                      userSelect: 'none'
                    }}
                  >
                    ›
                  </Typography>
                )}
                {active ? (
                  <Box
                    sx={{
                      display: 'inline-flex',
                      alignItems: 'center',
                      gap: '6px',
                      height: '34px',
                      pl: '9px',
                      pr: '13px',
                      py: '7px',
                      borderRadius: '100px',
                      border: '1px solid #D7DEE4',
                      backgroundColor: '#fff',
                      boxShadow: '0px 1px 1px rgba(0, 0, 0, 0.05)'
                    }}
                  >
                    <Box
                      sx={{
                        width: 20,
                        height: 20,
                        borderRadius: '10px',
                        backgroundColor: '#507CE1',
                        color: '#fff',
                        fontSize: '11.5px',
                        fontWeight: 500,
                        display: 'flex',
                        alignItems: 'center',
                        justifyContent: 'center',
                        flexShrink: 0
                      }}
                    >
                      {index + 1}
                    </Box>
                    <CloudIconBox code={provider.code} size={16} />
                    <Typography sx={{ ...providerLabelSx, color: '#0B1117' }}>
                      {codeLabel}
                    </Typography>
                  </Box>
                ) : configured ? (
                  <Box
                    sx={{
                      display: 'inline-flex',
                      alignItems: 'center',
                      gap: '6px',
                      height: '34px',
                      px: '4px',
                      py: '6px'
                    }}
                    data-testid={`configure-instances-step-done-${provider.uuid}`}
                  >
                    <Box
                      aria-hidden
                      sx={{
                        width: 24,
                        height: 24,
                        borderRadius: '50%',
                        backgroundColor: '#82D2B0',
                        display: 'flex',
                        alignItems: 'center',
                        justifyContent: 'center',
                        flexShrink: 0,
                        '& svg': { width: 16, height: 16, display: 'block' }
                      }}
                    >
                      <CheckWhiteIcon style={{ width: '24px', height: '24px' }}/>
                    </Box>
                    <Typography sx={{ ...providerLabelSx, color: '#4E5F6D' }}>
                      {codeLabel}
                    </Typography>
                  </Box>
                ) : (
                  <Box
                    sx={{
                      display: 'inline-flex',
                      alignItems: 'center',
                      gap: '6px',
                      height: '32px',
                      px: '4px',
                      py: '6px'
                    }}
                  >
                    <CloudIconBox code={provider.code} size={16} />
                    <Typography sx={{ ...providerLabelSx, color: '#4E5F6D' }}>
                      {codeLabel}
                    </Typography>
                  </Box>
                )}
              </Box>
            );
          })}
        </Box>

        {seeded && (
          <InstanceSettingsForm
            key={`${current.uuid}-${viewMode}`}
            ref={formRef}
            embedded
            viewMode={viewMode}
            providerOverride={current}
            regionsOverride={regionsByProvider[current.uuid] ?? []}
            formDefaults={draft[current.uuid] ?? defaultInstanceSettings()}
            onValidated={handleValidated}
          />
        )}
      </Box>
    </YBModal>
  );
}
