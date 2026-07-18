import { useLayoutEffect, useMemo, useRef, useState } from 'react';
import { Trans, useTranslation } from 'react-i18next';
import { useWatch } from 'react-hook-form';
import { keyframes } from '@mui/material/styles';
import InfoIcon from '@app/redesign/assets/info-message.svg';
import { mui, YBTooltip } from '@yugabyte-ui-library/core';
import { ReplicationFactorField } from '../../fields';
import { REPLICATION_FACTOR } from '../../fields/FieldNames';

const { Box, Typography, Link, IconButton } = mui;

const ACCENT = '#836BFF';

const valueColorPulse = keyframes`
  0% {
    transform: scale(1);
    color: #836bff;
  }
  50% {
    transform: scale(1.08);
    color: #ed35c5;
  }
  100% {
    transform: scale(1);
    color: #836bff;
  }
`;

type ExpertNodesRequirementHintProps = {
  replicationFactor: number;
  showError: boolean;
};

function ExpertNodesRequirementHint({
  replicationFactor,
  showError
}: ExpertNodesRequirementHintProps) {
  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.nodesAndAvailability.expertMode'
  });
  const requirementColor = showError ? 'error.500' : 'grey.600';
  const emphasizedColor = showError ? 'error.500' : ACCENT;

  const [valuePulseId, setValuePulseId] = useState(0);
  const prevRfRef = useRef<number | null>(null);

  useLayoutEffect(() => {
    const prev = prevRfRef.current;
    prevRfRef.current = replicationFactor;
    if (prev === null || prev === replicationFactor) {
      return;
    }
    setValuePulseId((n) => n + 1);
  }, [replicationFactor]);

  const emphasizedSx = useMemo(
    () => ({
      color: emphasizedColor,
      fontWeight: 600,
      display: 'inline-block',
      transformOrigin: 'center center',
      ...(valuePulseId > 0
        ? { animation: `${valueColorPulse} 0.8s ease-out` }
        : {})
    }),
    [emphasizedColor, valuePulseId]
  );

  return (
    <Typography
      key={valuePulseId}
      variant="body2"
      component="div"
      sx={{
        fontSize: '13px',
        lineHeight: '16px',
        fontWeight: 400,
        color: requirementColor
      }}
    >
      <Trans
        t={t}
        i18nKey={replicationFactor > 1 ? 'requirementHint' : 'requirementHintSingleAZ'}
        values={{ rf: replicationFactor }}
        components={{
          rf: <Box component="span" sx={emphasizedSx} />,
          az: <Box component="span" sx={emphasizedSx} />,
          nodes: <Box component="span" sx={emphasizedSx} />
        }}
      />
    </Typography>
  );
}

type Props = {
  regionCount: number;
  effectiveReplicationFactor: number;
  showRequirementHintError?: boolean;
};

export function ExpertNodesReplicationSection({
  regionCount,
  effectiveReplicationFactor,
  showRequirementHintError = false
}: Props) {
  const rfFromForm = useWatch({ name: REPLICATION_FACTOR });
  const rf = useMemo(() => {
    if (typeof rfFromForm === 'number' && Number.isFinite(rfFromForm)) {
      return rfFromForm;
    }
    if (typeof rfFromForm === 'string' && rfFromForm !== '') {
      const parsed = parseInt(rfFromForm, 10);
      if (!Number.isNaN(parsed)) {
        return parsed;
      }
    }
    return typeof effectiveReplicationFactor === 'number' &&
      Number.isFinite(effectiveReplicationFactor)
      ? effectiveReplicationFactor
      : 1;
  }, [rfFromForm, effectiveReplicationFactor]);

  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.nodesAndAvailability.expertMode'
  });
  const getDisabledRfTooltip = (opt: string) =>
    parseInt(opt, 10) < regionCount ? t('disabledRfTooltip', { rf: opt }) : undefined;

  return (
    <Box sx={{ display: 'flex', flexDirection: 'column', gap: '16px', alignItems: 'flex-start' }}>
      <ReplicationFactorField
        replication_options={['1', '3', '5', '7']}
        fieldName={REPLICATION_FACTOR}
        isOptionDisabled={(opt) => parseInt(opt, 10) < regionCount}
        getOptionTooltip={getDisabledRfTooltip}
        label={
          <Box sx={{ display: 'flex', alignItems: 'center', gap: '4px' }}>
            <Typography
              component="span"
              sx={{
                fontSize: '13px',
                fontWeight: 500,
                lineHeight: '16px',
                color: 'grey.600'
              }}
            >
              {t('replicationFactorLabel')}
            </Typography>
            <YBTooltip title={
              <Trans
                t={t}
                i18nKey="replicationFactorInfo"
                components={{
                  a: <Link
                    href={"#"}
                    rel="noopener noreferrer"
                    underline="always"
                    onClick={(e) => e.stopPropagation()}
                    sx={{ fontSize: '11.5px', lineHeight: '16px' }}
                  />
                }}
              />
            }>
              <IconButton
                size="small"
                aria-label={t('replicationFactorInfoAria')}
                style={{ padding: 2 }}
              >
                <InfoIcon style={{ fontSize: 18, color: '#6D7C88' }} />
              </IconButton>
            </YBTooltip>
          </Box>
        }
      />
      <ExpertNodesRequirementHint
        replicationFactor={rf}
        showError={showRequirementHintError}
      />
    </Box>
  );
}
