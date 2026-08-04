import { Trans } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { AlertVariant, YBAlert } from '@yugabyte-ui-library/core';
import { AvailabilityZones } from './AvailabilityZones';
import { DedicatedNode } from './DedicatedNodes';
import { TotalNodeCount } from './TotalNodeCount';
import { ResilienceRequirementCard } from '../resilence-regions/ResilienceRequirementCard';
import { ResilienceFormMode, ResilienceType } from '../resilence-regions/dtos';
import { Region } from '../../../../../helpers/dtos';
import type { UseNodesAvailabilityStepResult } from './useNodesAvailabilityStep';
import { NodesAvailabilityMapSection } from './NodesAvailabilityMapSection';
import { useAvailabilityZonesRegionCards } from './useAvailabilityZonesRegionCards';
import {
  GuidedNodesAvailabilityDefaultLayout,
  GuidedNodesAvailabilityGeoLayout
} from './NodesAvailabilityGuidedLayouts';

type Props = Pick<
  UseNodesAvailabilityStepResult,
  | 'regions'
  | 'icon'
  | 'showErrorsAfterSubmit'
  | 'lesserNodesTransValues'
  | 'errors'
  | 't'
  | 'resilienceAndRegionsSettings'
> & {
  isGeoPartition?: boolean;
};

export function NodesAvailabilityGuidedBody({
  regions,
  icon,
  showErrorsAfterSubmit,
  lesserNodesTransValues,
  errors,
  t,
  resilienceAndRegionsSettings,
  isGeoPartition = false
}: Props) {
  const {
    formState: { errors: formErrors }
  } = useFormContext();
  const { azCount, faultToleranceNeeded, regionCards } = useAvailabilityZonesRegionCards({
    mode: ResilienceFormMode.GUIDED,
    showErrorsAfterSubmit,
    resilienceAndRegionsSettings
  });

  const showRequirementCard =
    resilienceAndRegionsSettings?.resilienceType === ResilienceType.REGULAR &&
    resilienceAndRegionsSettings?.resilienceFormMode === ResilienceFormMode.GUIDED;

  const map = (
    <NodesAvailabilityMapSection
      regions={regions as Region[]}
      icon={icon}
      mapHeight={isGeoPartition ? 362 : undefined}
    />
  );

  const requirementCard = showRequirementCard ? (
    <ResilienceRequirementCard
      resilienceAndRegionsProps={resilienceAndRegionsSettings!}
      noShadow
      placementStep="nodes"
    />
  ) : null;

  const availabilityZones = (
    <AvailabilityZones
      showErrorsAfterSubmit={showErrorsAfterSubmit}
      showAvailabilityZonesError={Boolean((formErrors as any)?.availabilityZones?.message)}
      azCount={azCount}
      faultToleranceNeeded={faultToleranceNeeded}
      bottomContent={<TotalNodeCount />}
    >
      {regionCards}
    </AvailabilityZones>
  );

  const lesserNodesAlert =
    showErrorsAfterSubmit && (errors as any)?.lesserNodes?.message ? (
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
    ) : null;

  const dedicatedNode = <DedicatedNode />;

  const slots = {
    map,
    requirementCard,
    availabilityZones,
    lesserNodesAlert,
    dedicatedNode
  };

  const Layout = isGeoPartition ? GuidedNodesAvailabilityGeoLayout : GuidedNodesAvailabilityDefaultLayout;

  return <Layout {...slots} />;
}
