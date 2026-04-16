import { Trans } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { AlertVariant, mui, YBAlert } from '@yugabyte-ui-library/core';
import { AvailabilityZones } from './AvailabilityZones';
import { DedicatedNode } from './DedicatedNodes';
import { TotalNodeCount } from './TotalNodeCount';
import { ResilienceRequirementCard } from '../resilence-regions/ResilienceRequirementCard';
import { ResilienceFormMode, ResilienceType } from '../resilence-regions/dtos';
import { Region } from '../../../../../helpers/dtos';
import type { UseNodesAvailabilityStepResult } from './useNodesAvailabilityStep';
import { NodesAvailabilityMapSection } from './NodesAvailabilityMapSection';
import { useAvailabilityZonesRegionCards } from './useAvailabilityZonesRegionCards';

const { Box } = mui;

type Props = Pick<
  UseNodesAvailabilityStepResult,
  | 'regions'
  | 'icon'
  | 'showErrorsAfterSubmit'
  | 'lesserNodesTransValues'
  | 'errors'
  | 't'
  | 'resilienceAndRegionsSettings'
>;

export function NodesAvailabilityGuidedBody({
  regions,
  icon,
  showErrorsAfterSubmit,
  lesserNodesTransValues,
  errors,
  t,
  resilienceAndRegionsSettings
}: Props) {
  const {
    formState: { errors: formErrors }
  } = useFormContext();
  const { azCount, faultToleranceNeeded, regionCards } = useAvailabilityZonesRegionCards({
    mode: ResilienceFormMode.GUIDED,
    showErrorsAfterSubmit,
    resilienceAndRegionsSettings
  });

  return (
    <Box sx={{ display: 'flex', flexDirection: 'column', gap: '24px' }}>
      <NodesAvailabilityMapSection regions={regions as Region[]} icon={icon} />
      {resilienceAndRegionsSettings?.resilienceType === ResilienceType.REGULAR &&
        (resilienceAndRegionsSettings?.resilienceFormMode ===
          ResilienceFormMode.GUIDED) && (
          <ResilienceRequirementCard
            resilienceAndRegionsProps={resilienceAndRegionsSettings}
            noShadow
            placementStep="nodes"
          />
        )}
      <AvailabilityZones
        showErrorsAfterSubmit={showErrorsAfterSubmit}
        showAvailabilityZonesError={Boolean((formErrors as any)?.availabilityZones?.message)}
        azCount={azCount}
        faultToleranceNeeded={faultToleranceNeeded}
        bottomContent={<TotalNodeCount />}
      >
        {regionCards}
      </AvailabilityZones>
      {showErrorsAfterSubmit && (errors as any)?.lesserNodes?.message && (
        <YBAlert
          open
          variant={AlertVariant.Error}
          text={
            <Trans
              t={t}
              i18nKey={(errors as any)?.lesserNodes?.message}
              components={{ b: <b /> }}
              values={lesserNodesTransValues}
            >
              {(errors as any).lesserNodes.message}
            </Trans>
          }
        />
      )}
      <DedicatedNode />
    </Box>
  );
}
