import { FC, useMemo, useRef, useState } from 'react';
import { keyframes } from '@mui/material/styles';
import { mui, YBTag } from '@yugabyte-ui-library/core';
import { useTranslation } from 'react-i18next';
import pluralize from 'pluralize';
import { FaultToleranceType, ResilienceAndRegionsProps } from './dtos';
import {
  getGuidedResilienceRequirementSummary,
  getNodesStepRequirementCardTitleSpec,
  type GuidedRequirementTag,
  type RequirementCardPlacementStep
} from './GuidedResilienceRequirementSummary';
import Wrench from '@app/redesign/assets/wrench2.svg';
import QuestionIcon from '@app/redesign/assets/question_circled.svg?img';
import { ReplicationFactorInfoModal } from '../nodes-availability';

const { Box, styled, Typography, Divider } = mui;

interface RootProps {
  noShadow?: boolean;
}

const Root = styled(Box, {
  shouldForwardProp: (prop) => prop !== 'noShadow'
})<RootProps>(({ theme, noShadow }) => ({
  padding: '16px',
  borderRadius: '16px',
  border: `1px solid ${theme.palette.grey[200]}`,
  boxShadow: noShadow ? 'none' : '0 1px 3px 0 rgba(0, 0, 0, 0.08), 0 1px 2px 0 rgba(0, 0, 0, 0.05)',
  display: 'flex',
  gap: '16px'
}));

const tagHighlight = keyframes`
  0% {
    transform: scale(1);
    background-color: #ffffff;
  }
  50% {
    transform: scale(1.06);
    background-color: #E8E9FE;
  }
  100% {
    transform: scale(1);
    background-color: #ffffff;
  }
`;

const tagBaseSx = {
  color: '#735AF5',
  fontWeight: '500',
  lineHeight: '16px',
  transformOrigin: 'center center'
};

function serializeRequirementTag(tag: GuidedRequirementTag) {
  return JSON.stringify(tag);
}

/** Slot identity: same tag payload can mean different requirements across FT modes (e.g. AZ×1 alone vs with nodes). */
function requirementTagSlotSignature(
  tag: GuidedRequirementTag,
  faultToleranceType: FaultToleranceType,
  tagListLength: number
) {
  return `${serializeRequirementTag(tag)}|${faultToleranceType}|${tagListLength}`;
}

function tagLabel(tag: GuidedRequirementTag, t: (k: string, o?: Record<string, unknown>) => string) {
  switch (tag.kind) {
    case 'regions':
      return t('requirementTagRegions', {
        count: tag.count,
        entity: pluralize(t('wordRegion'), tag.count)
      });
    case 'regions_one_plus':
      return t('requirementTagRegionsOnePlus', {
        entity: pluralize(t('wordRegion'), 2)
      });
    case 'availability_zones':
      return t('requirementTagAvailabilityZones', {
        count: tag.count,
        entity: pluralize(t('wordAvailabilityZone'), tag.count)
      });
    case 'nodes_minimum':
      return t('requirementTagNodesMinimum', {
        count: tag.count,
        entity: pluralize(t('wordNode'), tag.count)
      });
    default:
      return '';
  }
}

interface ResilienceRequirementCardProps {
  resilienceAndRegionsProps: ResilienceAndRegionsProps;
  noShadow?: boolean;
  placementStep?: RequirementCardPlacementStep;
}

export const ResilienceRequirementCard: FC<ResilienceRequirementCardProps> = ({
  resilienceAndRegionsProps,
  noShadow = false,
  placementStep = 'resilience'
}) => {
  const { resilienceFactor, faultToleranceType } = resilienceAndRegionsProps;

  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.resilienceAndRegions.guidedMode'
  });
  const [showReplicationFactorInfoModal, setShowReplicationFactorInfoModal] = useState(false);
  const prevTagSignaturesRef = useRef<string[] | null>(null);
  const tagPlayCountRef = useRef<number[]>([]);

  const summary = useMemo(
    () => getGuidedResilienceRequirementSummary(faultToleranceType, resilienceFactor),
    [faultToleranceType, resilienceFactor]
  );

  const titleNode = useMemo(() => {
    if (placementStep === 'resilience') {
      return t('selectedResilienceRequires');
    }
    const titleSpec = getNodesStepRequirementCardTitleSpec(faultToleranceType, resilienceFactor);
    if (!titleSpec) {
      return t('selectedResilienceRequires');
    }
    return t('nodesStepTitleOutageResilience', {
      count: titleSpec.count,
      entity: pluralize(t(titleSpec.entityWordKey), titleSpec.count)
    });
  }, [placementStep, faultToleranceType, resilienceFactor, t]);

  const tagSignatures = summary.tags.map((tag) =>
    requirementTagSlotSignature(tag, faultToleranceType, summary.tags.length)
  );
  const prevSignatures = prevTagSignaturesRef.current;
  if (prevSignatures) {
    for (let i = 0; i < tagSignatures.length; i++) {
      if (tagSignatures[i] !== prevSignatures[i]) {
        const playCounts = tagPlayCountRef.current;
        playCounts[i] = (playCounts[i] ?? 0) + 1;
      }
    }
  }
  tagPlayCountRef.current.length = tagSignatures.length;
  prevTagSignaturesRef.current = tagSignatures;

  return (
    <Root noShadow={noShadow}>
      <Wrench />
      <Box sx={{ display: 'flex', flexDirection: 'column', gap: '10px' }}>
        <Typography
          variant="body1"
          sx={(theme) => ({
            fontWeight: 500,
            lineHeight: '16px',
            color: theme.palette.grey[900]
          })}
        >
          {titleNode}
        </Typography>
        <Box sx={{ display: 'flex', flexDirection: 'row', gap: '12px', alignItems: 'center', flexWrap: 'wrap' }}>
          <Box sx={{ display: 'flex', flexDirection: 'row', gap: '8px', alignItems: 'center', flexWrap: 'wrap' }}>
            {summary.tags.map((tag, i) => {
              const playCount = tagPlayCountRef.current[i] ?? 0;
              return (
                <YBTag
                  key={`${i}-${tagSignatures[i]}-${playCount}`}
                  size="medium"
                  variant="light"
                  customSx={
                    playCount > 0
                      ? {
                          ...tagBaseSx,
                          animation: `${tagHighlight} 0.6s ease-out`
                        }
                      : tagBaseSx
                  }
                >
                  {tagLabel(tag, t)}
                </YBTag>
              );
            })}
          </Box>
          <Divider
            orientation="vertical"
            sx={{ margin: '0px 4px', height: '12px', borderColor: '#D7DEE4', width: '1px' }}
          />
          <Box sx={{ display: 'flex', flexDirection: 'row', gap: '4px', alignItems: 'center' }}>
            <Typography
              variant="body2"
              sx={(theme) => ({
                fontWeight: 400,
                lineHeight: '16px',
                color: theme.palette.grey[600]
              })}
            >
              {t('automaticReplicationFactor', {
                replication_factor: summary.displayReplicationFactor
              })}
            </Typography>
            <img
              src={QuestionIcon}
              alt=""
              style={{ marginLeft: '-4px', cursor: 'pointer' }}
              onClick={() => setShowReplicationFactorInfoModal(true)}
            />
          </Box>
        </Box>
      </Box>
      <ReplicationFactorInfoModal
        onClose={() => setShowReplicationFactorInfoModal(false)}
        open={showReplicationFactorInfoModal}
      />
    </Root>
  );
};
