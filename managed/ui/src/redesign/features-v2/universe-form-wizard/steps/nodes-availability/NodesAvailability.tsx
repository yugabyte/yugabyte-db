import { forwardRef, useContext, useImperativeHandle } from 'react';
import { FormProvider, useForm } from 'react-hook-form';
import { useMount } from 'react-use';
import { mui } from '@yugabyte-ui-library/core';

import {
  YBMaps,
  YBMapMarker,
  MarkerType,
  MapLegend,
  MapLegendItem,
  useGetMapIcons
} from '@yugabyte-ui-library/core';
import { Region } from '../../../../helpers/dtos';
import { FreeFormRFRequirement } from './FreeFormRFRequirement';
import {
  CreateUniverseContext,
  CreateUniverseContextMethods,
  StepsRef
} from '../../CreateUniverseContext';
import { GuidedRequirementDetails } from './GuidedRequirementDetails';
import { AvailabilityZones } from './AvailabilityZones';
import { ReplicationStatusCard } from './ReplicationStatusCard';
import { DedicatedNode } from './DedicatedNodes';
import { ResilienceFormMode, ResilienceType } from '../resilence-regions/dtos';
import { assignRegionsAZNodeByReplicationFactor } from '../../CreateUniverseUtils';
import { NodeAvailabilityProps } from './dtos';

const { Box } = mui;

export const NodesAvailability = forwardRef<StepsRef>((_, forwardRef) => {
  const [
    { resilienceAndRegionsSettings, nodesAvailabilitySettings },
    { moveToPreviousPage, moveToNextPage, saveNodesAvailabilitySettings }
  ] = (useContext(CreateUniverseContext) as unknown) as CreateUniverseContextMethods;

  const regions = resilienceAndRegionsSettings?.regions ?? [];
  const icon = useGetMapIcons({ type: MarkerType.REGION_SELECTED });

  const methods = useForm<NodeAvailabilityProps>({
    defaultValues: nodesAvailabilitySettings
  });

  useMount(() => {
    if (!resilienceAndRegionsSettings) return;

    const availabilityZones = assignRegionsAZNodeByReplicationFactor(resilienceAndRegionsSettings);
    methods.setValue('availabilityZones', availabilityZones);
  });

  useImperativeHandle(
    forwardRef,
    () => ({
      onNext: () => {
        methods.handleSubmit((data) => {
          saveNodesAvailabilitySettings(data);
          moveToNextPage();
        })();
      },
      onPrev: () => {
        moveToPreviousPage();
      }
    }),
    []
  );

  return (
    <FormProvider {...methods}>
      <Box sx={{ display: 'flex', flexDirection: 'column', gap: '24px' }}>
        <YBMaps
          mapHeight={345}
          coordinates={[
            [37.3688, -122.0363],
            [34.052235, -118.243683]
          ]}
          initialBounds={undefined}
          defaultZoom={5}
        >
          {
            regions?.map((region: Region) => {
              return (
                <YBMapMarker
                  key={region.code}
                  position={[region.latitude, region.longitude]}
                  type={MarkerType.REGION_SELECTED}
                  tooltip={<>{region.name}</>}
                />
              );
            }) as any
          }
          {regions?.length > 0 && (
            <MapLegend
              mapLegendItems={[<MapLegendItem icon={<>{icon.normal}</>} label={'Region'} />]}
            />
          )}
        </YBMaps>
        {resilienceAndRegionsSettings?.resilienceType === ResilienceType.REGULAR &&
          resilienceAndRegionsSettings?.resilienceFormMode === ResilienceFormMode.GUIDED && (
            <ReplicationStatusCard />
          )}
        {resilienceAndRegionsSettings?.resilienceFormMode === ResilienceFormMode.FREE_FORM ? (
          <FreeFormRFRequirement />
        ) : (
          <GuidedRequirementDetails />
        )}
        <AvailabilityZones />
        <DedicatedNode />
      </Box>
    </FormProvider>
  );
});

NodesAvailability.displayName = 'NodesAvailability';
