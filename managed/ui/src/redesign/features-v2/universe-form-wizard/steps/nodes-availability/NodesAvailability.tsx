import { forwardRef, useContext, useImperativeHandle } from 'react';
import { FormProvider, useForm } from 'react-hook-form';
import { useMount } from 'react-use';
import { AlertVariant, mui, YBAlert } from '@yugabyte-ui-library/core';

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
import { NodesAvailabilitySchema } from './ValidationSchema';
import { yupResolver } from '@hookform/resolvers/yup';
import { Trans, useTranslation } from 'react-i18next';

const { Box } = mui;

export const NodesAvailability = forwardRef<StepsRef>((_, forwardRef) => {
  const [
    { resilienceAndRegionsSettings, nodesAvailabilitySettings },
    { moveToPreviousPage, moveToNextPage, saveNodesAvailabilitySettings }
  ] = (useContext(CreateUniverseContext) as unknown) as CreateUniverseContextMethods;

  const { t } = useTranslation("translation", { keyPrefix: "createUniverseV2.nodesAndAvailability" });
  const regions = resilienceAndRegionsSettings?.regions ?? [];
  const icon = useGetMapIcons({ type: MarkerType.REGION_SELECTED });

  const methods = useForm<NodeAvailabilityProps>({
    defaultValues: nodesAvailabilitySettings,
    resolver: yupResolver(NodesAvailabilitySchema(t, resilienceAndRegionsSettings))
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
  const { errors } = methods.formState;
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
          dataTestId='yb-maps-nodes-availability'
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
          {regions?.length > 0 ? (
            <MapLegend
              mapLegendItems={[<MapLegendItem icon={<>{icon.normal}</>} label={'Region'} />]}
            />
          ): <span/>}
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
        {(errors as any)?.lesserNodes?.message && (
          <YBAlert
            open
            variant={AlertVariant.Error}
            text={
              <Trans
                t={t}
                i18nKey={(errors as any)?.lesserNodes?.message}
                components={{ b: <b /> }}
              >
                {(errors as any).lesserNodes.message}
              </Trans>
            }
          />
      )}
        <DedicatedNode />
      </Box>
    </FormProvider>
  );
});

NodesAvailability.displayName = 'NodesAvailability';
