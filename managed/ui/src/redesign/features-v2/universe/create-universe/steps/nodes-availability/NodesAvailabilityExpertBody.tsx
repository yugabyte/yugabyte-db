import { Trans } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { AlertVariant, YBAlert } from '@yugabyte-ui-library/core';
import { AvailabilityZones } from './AvailabilityZones';
import { DedicatedNode } from './DedicatedNodes';
import { TotalNodeCount } from './TotalNodeCount';
import { Region } from '../../../../../helpers/dtos';
import type { UseNodesAvailabilityStepResult } from './useNodesAvailabilityStep';
import { NodesAvailabilityMapSection } from './NodesAvailabilityMapSection';
import { ResilienceFormMode } from '../resilence-regions/dtos';
import { useAvailabilityZonesRegionCards } from './useAvailabilityZonesRegionCards';
import { ExpertNodesReplicationSection } from './ExpertNodesReplicationSection';
import { InferredResilienceCard } from './InferredResilienceCard';
import { NodeAvailabilityProps } from './dtos';
import {
  ExpertNodesAvailabilityDefaultLayout,
  ExpertNodesAvailabilityGeoLayout
} from './NodesAvailabilityExpertLayouts';

type Props = Pick<
  UseNodesAvailabilityStepResult,
  | 'regions'
  | 'icon'
  | 'showErrorsAfterSubmit'
  | 'lesserNodesTransValues'
  | 'errors'
  | 't'
  | 'inferredResilience'
  | 'effectiveReplicationFactor'
  | 'resilienceAndRegionsSettings'
> & {
  isGeoPartition?: boolean;
};

export function NodesAvailabilityExpertBody({
  regions,
  icon,
  showErrorsAfterSubmit,
  lesserNodesTransValues,
  errors,
  t,
  inferredResilience,
  effectiveReplicationFactor,
  resilienceAndRegionsSettings,
  isGeoPartition = false
}: Props) {
  const { watch } = useFormContext<NodeAvailabilityProps>();
  const availabilityZonesForm = watch('availabilityZones');
  const { azCount, faultToleranceNeeded, regionCards } = useAvailabilityZonesRegionCards({
    mode: ResilienceFormMode.EXPERT_MODE,
    showErrorsAfterSubmit,
    resilienceAndRegionsSettings
  });
  const hasLesserNodesError = showErrorsAfterSubmit && Boolean((errors as any)?.lesserNodes?.message);

  const map = (
    <NodesAvailabilityMapSection
      regions={regions as Region[]}
      icon={icon}
      mapHeight={isGeoPartition ? 362 : undefined}
    />
  );

  const availabilityZones = (
    <AvailabilityZones
      showErrorsAfterSubmit={showErrorsAfterSubmit}
      showAvailabilityZonesError={false}
      azCount={azCount}
      faultToleranceNeeded={faultToleranceNeeded}
      topContent={
        <ExpertNodesReplicationSection
          regionCount={resilienceAndRegionsSettings?.regions?.length ?? 0}
          effectiveReplicationFactor={effectiveReplicationFactor}
          showRequirementHintError={
            hasLesserNodesError &&
            (errors as any)?.lesserNodes?.message === 'errMsg.expertResilienceUninferable'
          }
        />
      }
      bottomContent={
        <>
          <TotalNodeCount />
          <InferredResilienceCard
            inferredResilience={inferredResilience ?? null}
            replicationFactor={effectiveReplicationFactor}
            availabilityZones={availabilityZonesForm ?? {}}
          />
        </>
      }
    >
      {regionCards}
    </AvailabilityZones>
  );

  const lesserNodesAlert = hasLesserNodesError ? (
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
    availabilityZones,
    lesserNodesAlert,
    dedicatedNode
  };

  const Layout = isGeoPartition
    ? ExpertNodesAvailabilityGeoLayout
    : ExpertNodesAvailabilityDefaultLayout;

  return <Layout {...slots} />;
}
