import { FC, useMemo } from 'react';
import pluralize from 'pluralize';
import { Trans, useTranslation } from 'react-i18next';
import { NodeAvailabilityProps } from './dtos';
import { FaultToleranceType } from '../resilence-regions/dtos';
import { mui } from '@yugabyte-ui-library/core';
import { getInferredOutageCount, inferResilience } from '../../CreateUniverseUtils';

import CheckBlueIcon from '../../../../../assets/check_blue.svg';
import CautionIcon from '../../../../../assets/caution.svg';

const { Box, styled, Typography } = mui;

type InferredResilience = NonNullable<ReturnType<typeof inferResilience>>;

type InferredResilienceCardProps = {
  inferredResilience: InferredResilience | null;
  replicationFactor: number;
  availabilityZones: NodeAvailabilityProps['availabilityZones'];
};

const Root = styled(Box, {
  shouldForwardProp: (prop) => prop !== 'notResilient'
})<{ notResilient: boolean }>(({ theme, notResilient }) => ({
  display: 'flex',
  alignItems: 'center',
  gap: '16px',
  padding: '12px 16px',
  borderRadius: '8px',
  border: `1px solid ${notResilient ? theme.palette.warning[100] : theme.palette.primary[200]}`,
  backgroundColor: notResilient ? '#FFF6E3' : theme.palette.primary[100],
  width: '672px'
}));

const IconBubble = styled(Box, {
  shouldForwardProp: (prop) => prop !== 'notResilient'
})<{ notResilient: boolean }>(({ theme, notResilient }) => ({
  display: 'flex',
  alignItems: 'center',
  justifyContent: 'center',
  borderRadius: '16px',
  padding: '4px',
  backgroundColor: notResilient ? theme.palette.warning[100] : theme.palette.primary[200]
}));

const textSx = {
  fontSize: '13px',
  lineHeight: '16px',
  color: 'grey.900',
  fontWeight: 400
} as const;

function getOutageLevelKey(inferredResilience: InferredResilience) {
  switch (inferredResilience) {
    case FaultToleranceType.REGION_LEVEL:
      return 'regionOutage';
    case FaultToleranceType.AZ_LEVEL:
      return 'availabilityZoneOutage';
    default:
      return 'nodeOutage';
  }
}

export const InferredResilienceCard: FC<InferredResilienceCardProps> = ({
  inferredResilience,
  replicationFactor,
  availabilityZones
}) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.nodesAndAvailability.inferredResilienceCard'
  });
  const outageCount = getInferredOutageCount(
    inferredResilience,
    replicationFactor,
    availabilityZones
  );
  const notResilient = replicationFactor <= 1;
  const cardVisible = notResilient || (inferredResilience !== null && outageCount > 0);

  const outageLevelLabel = useMemo(() => {
    if (!inferredResilience) {
      return '';
    }
    return pluralize(t(getOutageLevelKey(inferredResilience)), outageCount);
  }, [inferredResilience, outageCount, t]);

  // For RF=1, explicitly show the not-resilient message.
  // Otherwise hide when resilience cannot be inferred or outage tolerance is zero.
  if (!cardVisible) {
    return null;
  }

  return (
    <Root notResilient={notResilient} data-testid="inferred-resilience-card">
      <IconBubble notResilient={notResilient}>
        {notResilient ? <CautionIcon /> : <CheckBlueIcon />}
      </IconBubble>
      {notResilient ? (
        <Typography sx={textSx}>
          <Trans t={t} i18nKey="notResilientBody" components={{ b: <b /> }} />
        </Typography>
      ) : (
        <Typography variant='body2' sx={textSx}>
          <Trans t={t} i18nKey="resilientMessage" values={{ count: outageCount, outageLevel: outageLevelLabel }} components={{ b: <b /> }} />
        </Typography>
      )}
    </Root>
  );
};
