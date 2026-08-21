import { useEffect, useMemo, useState } from 'react';
import { Trans, useTranslation } from 'react-i18next';
import { AlertVariant, mui, yba, YBAlert, YBTag, YBRadio, YBInput } from '@yugabyte-ui-library/core';
import { ResizeUpdateOption } from '../../../../../v2/api/yugabyteDBAnywhereV2APIs.schemas';

const { Box, Typography, styled } = mui;
const { YBModal } = yba;

export type UpdateStrategy = 'rolling' | 'migrate';

export interface ReviewHardwareConfirmPayload {
  delaySeconds: number;
  strategy: UpdateStrategy;
}

/** Keys for storage-related rows (volume count/size share one card). */
export type ChangedHardwareStorageKey =
  | 'volumeLayout'
  | 'diskIops'
  | 'throughput'
  | 'storageType'
  | 'storageClass'
  | 'mountPoints'
  | 'cpuCoreCount'
  | 'memoryGib';

export interface HardwareReviewSummary {
  instanceType?: string | null;
  instanceTypeLabel?: string | null;
  volumeSize?: number | null;
  numVolumes?: number | null;
  diskIops?: number | null;
  throughput?: number | null;
  storageClass?: string | null;
  storageType?: string | null;
  mountPoints?: string | null;
  cpuCoreCount?: number | null;
  memoryGib?: number | null;
}

export interface HardwareReviewSection {
  /** When set, a section title is shown (dedicated master / t-server). */
  headingKey?: 'tServerInstance' | 'masterServerInstance';
  current: HardwareReviewSummary;
  next: HardwareReviewSummary;
}

const normNum = (v: number | null | undefined) =>
  v === undefined || v === null || Number.isNaN(Number(v)) ? null : Number(v);

const normStr = (v: string | null | undefined) => {
  if (v === undefined || v === null) return null;
  const t = String(v).trim();
  return t.length ? t : null;
};

export const instanceTypeCodeChanged = (current: HardwareReviewSummary, next: HardwareReviewSummary) =>
  normStr(current.instanceType) !== normStr(next.instanceType);

export const getChangedStorageKeys = (
  current: HardwareReviewSummary,
  next: HardwareReviewSummary
): ChangedHardwareStorageKey[] => {
  const keys: ChangedHardwareStorageKey[] = [];
  if (normNum(current.numVolumes) !== normNum(next.numVolumes) || normNum(current.volumeSize) !== normNum(next.volumeSize)) {
    keys.push('volumeLayout');
  }
  if (normNum(current.diskIops) !== normNum(next.diskIops)) {
    keys.push('diskIops');
  }
  if (normNum(current.throughput) !== normNum(next.throughput)) {
    keys.push('throughput');
  }
  if (normStr(current.storageType) !== normStr(next.storageType)) {
    keys.push('storageType');
  }
  if (normStr(current.storageClass) !== normStr(next.storageClass)) {
    keys.push('storageClass');
  }
  if (normStr(current.mountPoints) !== normStr(next.mountPoints)) {
    keys.push('mountPoints');
  }
  if (normNum(current.cpuCoreCount) !== normNum(next.cpuCoreCount)) {
    keys.push('cpuCoreCount');
  }
  if (normNum(current.memoryGib) !== normNum(next.memoryGib)) {
    keys.push('memoryGib');
  }
  return keys;
};

export const hardwareReviewSectionHasVisibleChanges = (
  current: HardwareReviewSummary,
  next: HardwareReviewSummary
) => instanceTypeCodeChanged(current, next) || getChangedStorageKeys(current, next).length > 0;

export const canUseRollingResize = (options: ResizeUpdateOption[] | undefined) =>
  !!options?.includes(ResizeUpdateOption.SMART_RESIZE) ||
  !!options?.includes(ResizeUpdateOption.SMART_RESIZE_NON_RESTART);

export const canUseFullMove = (options: ResizeUpdateOption[] | undefined) =>
  !!options?.includes(ResizeUpdateOption.FULL_MOVE);

export const isNonRestartSmartResize = (options: ResizeUpdateOption[] | undefined) =>
  !!options?.includes(ResizeUpdateOption.SMART_RESIZE_NON_RESTART) &&
  !options?.includes(ResizeUpdateOption.SMART_RESIZE);

export const pickDefaultStrategy = (options: ResizeUpdateOption[] | undefined): UpdateStrategy =>
  canUseRollingResize(options) ? 'rolling' : 'migrate';

interface ReviewHardwareChangesModalProps {
  visible: boolean;
  isSubmitting?: boolean;
  sections: HardwareReviewSection[];
  initialDelaySeconds?: number;
  resizeOptions?: ResizeUpdateOption[];
  isLoadingOptions?: boolean;
  replicationFactor?: number;
  isK8s?: boolean;
  onClose: () => void;
  onConfirm: (payload: ReviewHardwareConfirmPayload) => void;
}

const SectionContainer = styled(Box)(({ theme }) => ({
  border: `1px solid ${theme.palette.grey[200]}`,
  borderRadius: '8px',
  padding: '20px',
  display: 'flex',
  gap: '20px'
}));

const SummaryColumn = styled(Box)(() => ({
  flex: 1,
  display: 'flex',
  flexDirection: 'column',
  gap: '16px'
}));

const SummaryCard = styled(Box)(({ theme }) => ({
  background: theme.palette.grey[50],
  border: `1px solid ${theme.palette.grey[200]}`,
  borderRadius: '8px',
  padding: '16px'
}));

const OptionsContainer = styled(Box)(({ theme }) => ({
  background: theme.palette.grey[50],
  border: `1px solid ${theme.palette.grey[200]}`,
  borderRadius: '8px',
  padding: '20px',
  display: 'flex',
  gap: '20px'
}));

const OptionLabel = styled(Typography)(() => ({
  fontSize: '13px',
  lineHeight: '16px',
  fontWeight: 400
}));

const Label = styled(Typography)(({ theme }) => ({
  color: theme.palette.grey[900],
  fontSize: '13px',
  fontWeight: 500,
  lineHeight: '18px',
  marginBottom: '8px'
}));

const Divider = styled(Box)(({ theme }) => ({
  width: '1px',
  background: theme.palette.grey[200]
}));

const getVolumeDisplay = (summary: HardwareReviewSummary) => {
  const count = summary.numVolumes ?? '-';
  const size = summary.volumeSize ? `${summary.volumeSize} GB` : '-';
  return (
    <Box display="flex" alignItems="center" gap={1}>
      <YBTag size="medium" variant="dark" color="primary" customSx={{ background: '#E8E9FE' }}>
        {count}
      </YBTag>
      <Typography variant="body2">X</Typography>
      <YBTag size="medium" variant="dark" color="primary" customSx={{ background: '#E8E9FE' }}>
        {size}
      </YBTag>
    </Box>
  );
};

const renderStorageFieldValue = (
  key: ChangedHardwareStorageKey,
  summary: HardwareReviewSummary,
  tHw: (k: string, o?: Record<string, unknown>) => string
) => {
  switch (key) {
    case 'volumeLayout':
      return getVolumeDisplay(summary);
    case 'diskIops':
      return (
        <YBTag size="medium" variant="dark" color="primary" customSx={{ background: '#E8E9FE' }}>
          {summary.diskIops ?? '-'}
        </YBTag>
      );
    case 'throughput':
      return (
        <YBTag size="medium" variant="dark" color="primary" customSx={{ background: '#E8E9FE' }}>
          {summary.throughput === undefined || summary.throughput === null
            ? '-'
            : tHw('throughtputValue', { throughput: summary.throughput })}
        </YBTag>
      );
    case 'storageType':
      return (
        <YBTag size="medium" variant="dark" color="primary" customSx={{ background: '#E8E9FE' }}>
          {summary.storageType ?? '-'}
        </YBTag>
      );
    case 'storageClass':
      return (
        <YBTag size="medium" variant="dark" color="primary" customSx={{ background: '#E8E9FE' }}>
          {summary.storageClass ?? '-'}
        </YBTag>
      );
    case 'mountPoints':
      return (
        <Typography variant="body2" sx={{ wordBreak: 'break-word' }}>
          {summary.mountPoints ?? '-'}
        </Typography>
      );
    case 'cpuCoreCount':
      return (
        <YBTag size="medium" variant="dark" color="primary" customSx={{ background: '#E8E9FE' }}>
          {summary.cpuCoreCount ?? '-'}
        </YBTag>
      );
    case 'memoryGib':
      return (
        <YBTag size="medium" variant="dark" color="primary" customSx={{ background: '#E8E9FE' }}>
          {summary.memoryGib === undefined || summary.memoryGib === null
            ? '-'
            : tHw('memoryValue', { memory: summary.memoryGib })}
        </YBTag>
      );
    default:
      return null;
  }
};

const storageFieldLabel = (
  key: ChangedHardwareStorageKey,
  tReview: (k: string) => string,
  tHw: (k: string, o?: Record<string, unknown>) => string,
  isK8s: boolean
) => {
  switch (key) {
    case 'volumeLayout':
      return tReview(isK8s ? 'volumeAndPod' : 'volumeAndNode');
    case 'diskIops':
      return tHw(isK8s ? 'iopsPod' : 'iops');
    case 'throughput':
      return tHw(isK8s ? 'throughputPod' : 'throughput');
    case 'storageType':
      return tHw('ebsType');
    case 'storageClass':
      return tReview('storageClass');
    case 'mountPoints':
      return tReview('mountPoints');
    case 'cpuCoreCount':
      return tHw('cpuCores');
    case 'memoryGib':
      return tHw('memory');
    default:
      return '';
  }
};

export const ReviewHardwareChangesModal = ({
  visible,
  isSubmitting = false,
  sections,
  initialDelaySeconds = 240,
  resizeOptions,
  isLoadingOptions = false,
  replicationFactor,
  isK8s = false,
  onClose,
  onConfirm
}: ReviewHardwareChangesModalProps) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'editUniverse.hardware.reviewChangesModal'
  });
  const { t: tHw } = useTranslation('translation', {
    keyPrefix: 'editUniverse.hardware'
  });

  const canRolling = canUseRollingResize(resizeOptions);
  const canMigrate = canUseFullMove(resizeOptions);
  const nonRestart = isNonRestartSmartResize(resizeOptions);
  const isRf1 = (replicationFactor ?? 1) <= 1;
  const onlyFullMove = canMigrate && !canRolling;
  const onlyNonRestart = nonRestart && !canMigrate;
  const showUpdateOptions = !onlyFullMove && !onlyNonRestart;

  const [strategy, setStrategy] = useState<UpdateStrategy>(() => pickDefaultStrategy(resizeOptions));
  const [delaySecondsInput, setDelaySecondsInput] = useState(String(initialDelaySeconds));

  useEffect(() => {
    if (!visible) return;
    setStrategy(pickDefaultStrategy(resizeOptions));
    setDelaySecondsInput(String(initialDelaySeconds));
  }, [visible, initialDelaySeconds, resizeOptions]);

  useEffect(() => {
    if (strategy === 'rolling' && !canRolling && canMigrate) {
      setStrategy('migrate');
    } else if (strategy === 'migrate' && !canMigrate && canRolling) {
      setStrategy('rolling');
    }
  }, [strategy, canRolling, canMigrate]);

  const parsedDelaySeconds = useMemo(() => Number(delaySecondsInput), [delaySecondsInput]);
  const needsDelay = strategy === 'rolling' && !nonRestart && !isRf1;
  const isDelayValid = !needsDelay || (Number.isFinite(parsedDelaySeconds) && parsedDelaySeconds > 0);
  const hasValidStrategy =
    (strategy === 'rolling' && canRolling) || (strategy === 'migrate' && canMigrate);
  const isConfirmDisabled =
    isSubmitting || isLoadingOptions || !hasValidStrategy || !isDelayValid;

  const sectionBlocks = useMemo(() => {
    return sections.map((section, sectionIdx) => {
      const showInstance = instanceTypeCodeChanged(section.current, section.next);
      const changedStorageKeys = getChangedStorageKeys(section.current, section.next);
      const hasAny = showInstance || changedStorageKeys.length > 0;
      return { section, sectionIdx, showInstance, changedStorageKeys, hasAny };
    });
  }, [sections]);

  if (!visible) return null;

  return (
    <YBModal
      open={visible}
      onClose={onClose}
      title={t('title')}
      size="md"
      overrideHeight={'fit-content'}
      dialogContentProps={{ sx: { padding: '24px !important' } }}
      cancelLabel={t('cancel', { keyPrefix: 'common' })}
      onSubmit={() =>
        onConfirm({
          delaySeconds: needsDelay ? parsedDelaySeconds : initialDelaySeconds,
          strategy
        })
      }
      submitLabel={t('confirmAndApply')}
      buttonProps={{
        primary: {
          dataTestId: 'edit-hardware-confirm-and-apply',
          disabled: isConfirmDisabled
        }
      }}
      titleSeparator
    >
      <Box display="flex" flexDirection="column" gap={3}>
        <Typography variant="body2">{t('summary')}</Typography>
        {sectionBlocks.length === 0 || !sectionBlocks.some((b) => b.hasAny) ? (
          <Typography variant="body2" color="textSecondary">
            {t('noChangedFieldsSummary')}
          </Typography>
        ) : (
          <Box display="flex" flexDirection="column" gap={2}>
            {sectionBlocks.map(({ section, sectionIdx, showInstance, changedStorageKeys, hasAny }) => {
              if (!hasAny) return null;
              return (
                <Box key={sectionIdx} display="flex" flexDirection="column" gap={1}>
                  {section.headingKey ? (
                    <Typography variant="subtitle2" fontWeight={600} color="textSecondary">
                      {tHw(section.headingKey)}
                    </Typography>
                  ) : null}
                  <SectionContainer>
                    <SummaryColumn>
                      <Typography variant="body1" fontWeight={600}>
                        {t('current')}
                      </Typography>
                      {showInstance ? (
                        <SummaryCard>
                          <Label>{t('instanceType')}</Label>
                          <YBTag
                            size="medium"
                            variant="dark"
                            color="primary"
                            customSx={{ background: '#E8E9FE' }}
                          >
                            {section.current.instanceTypeLabel ?? section.current.instanceType ?? '-'}
                          </YBTag>
                        </SummaryCard>
                      ) : null}
                      {changedStorageKeys.map((key) => (
                        <SummaryCard key={`c-${sectionIdx}-${key}`}>
                          <Label>{storageFieldLabel(key, t, tHw, isK8s)}</Label>
                          {renderStorageFieldValue(key, section.current, tHw)}
                        </SummaryCard>
                      ))}
                    </SummaryColumn>
                    <Divider />
                    <SummaryColumn>
                      <Typography variant="body1" fontWeight={600}>
                        {t('new')}
                      </Typography>
                      {showInstance ? (
                        <SummaryCard>
                          <Label>{t('instanceType')}</Label>
                          <YBTag
                            size="medium"
                            variant="dark"
                            color="primary"
                            customSx={{ background: '#E8E9FE' }}
                          >
                            {section.next.instanceTypeLabel ?? section.next.instanceType ?? '-'}
                          </YBTag>
                        </SummaryCard>
                      ) : null}
                      {changedStorageKeys.map((key) => (
                        <SummaryCard key={`n-${sectionIdx}-${key}`}>
                          <Label>{storageFieldLabel(key, t, tHw, isK8s)}</Label>
                          {renderStorageFieldValue(key, section.next, tHw)}
                        </SummaryCard>
                      ))}
                    </SummaryColumn>
                  </SectionContainer>
                </Box>
              );
            })}
          </Box>
        )}
        {onlyFullMove ? (
          <YBAlert
            open
            variant={AlertVariant.Info}
            text={
              <Trans t={t} i18nKey="fullMoveInfo" components={{ strong: <strong /> }} />
            }
          />
        ) : null}
        {onlyNonRestart ? (
          <YBAlert
            open
            variant={AlertVariant.Info}
            text={
              <Trans t={t} i18nKey="nonRestartInfo" components={{ strong: <strong /> }} />
            }
          />
        ) : null}
        {showUpdateOptions ? (
          <OptionsContainer>
            <Typography variant="body1" fontWeight={600} sx={{ minWidth: '130px' }}>
              {t('universeUpdateOptions')}
            </Typography>
            <Divider />
            <Box flex={1} display="flex" flexDirection="column" gap={2.5}>
              <Box>
                <Box display="flex" alignItems="center" gap={1}>
                  <YBRadio
                    dataTestId="hardware-rolling-restart"
                    checked={strategy === 'rolling'}
                    onChange={() => setStrategy('rolling')}
                    value="rolling"
                    size="small"
                    disabled={!canRolling || isLoadingOptions}
                  />
                  <OptionLabel>{t(isRf1 ? 'restartNodes' : 'rollingRestart')}</OptionLabel>
                  {canRolling && !isRf1 ? (
                    <YBTag size="small" variant="light">
                      {t('fasterAndRecommended')}
                    </YBTag>
                  ) : null}
                </Box>
                {strategy === 'rolling' && canRolling ? (
                  isRf1 ? (
                    <Box pl={4} pt={1}>
                      <YBAlert
                        open
                        variant={AlertVariant.Error}
                        text={
                          <Trans
                            t={t}
                            i18nKey="rollingRestartRf1Warning"
                            components={{ strong: <strong /> }}
                          />
                        }
                      />
                    </Box>
                  ) : (
                    <Typography
                      variant="subtitle1"
                      color="textSecondary"
                      sx={{ marginTop: '8px', marginLeft: '24px' }}
                    >
                      {t('rollingRestartDescription')}
                    </Typography>
                  )
                ) : null}
                {strategy === 'rolling' && canRolling && needsDelay ? (
                  <Box display="flex" alignItems="center" gap={1} pl={3} pt={1}>
                    <Typography variant="body2">{t('delayBetweenNodes')}</Typography>
                    <YBInput
                      size="small"
                      dataTestId="hardware-delay-seconds-input"
                      value={delaySecondsInput}
                      onChange={(event) => setDelaySecondsInput(event.target.value)}
                      sx={{ width: '96px', height: '32px' }}
                      error={!isDelayValid}
                      inputProps={{ 'data-testid': 'hardware-delay-seconds-input' }}
                    />
                    <Typography variant="body2">{t('seconds')}</Typography>
                  </Box>
                ) : null}
              </Box>
              <Box>
                <Box display="flex" alignItems="center" gap={1}>
                  <YBRadio
                    dataTestId="hardware-migrate-nodes"
                    checked={strategy === 'migrate'}
                    onChange={() => setStrategy('migrate')}
                    value="migrate"
                    size="small"
                    disabled={!canMigrate || isLoadingOptions}
                  />
                  <OptionLabel>{t('migrateNodes')}</OptionLabel>
                </Box>
                <Typography
                  variant="subtitle1"
                  color="textSecondary"
                  sx={{ marginTop: '8px', marginLeft: '24px' }}
                >
                  {t('migrateNodesDescription')}
                </Typography>
              </Box>
            </Box>
          </OptionsContainer>
        ) : null}
      </Box>
    </YBModal>
  );
};
