import { toast } from 'react-toastify';
import { useContext } from 'react';
import { AddGeoPartitionContext, AddGeoPartitionContextMethods } from '../AddGeoPartitionContext';
import { useTranslation } from 'react-i18next';
import { useQueries } from 'react-query';
import { mui } from '@yugabyte-ui-library/core';

import { getUniverseResources, useEditUniverse } from '@app/v2/api/universe/universe';
import {
  ReviewAndSummaryComponent,
  ReviewItem
} from '../../../create-universe/steps/review-summary/ReviewAndSummaryComponent';

import { ClusterType, Region } from '@app/redesign/helpers/dtos';
import { ClusterPartitionSpec, Universe } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

import GeoPartitionBreadCrumb from '../GeoPartitionBreadCrumbs';

import { UniverseActionButtons } from '../../../create-universe/components/UniverseActionButtons';
import { prepareAddGeoPartitionPayload, useGeoPartitionNavigation } from '../AddGeoPartitionUtils';
import { YBLoadingCircleIcon } from '@app/components/common/indicators';
import PinIcon from '@app/redesign/assets/pin.svg';

const { Box } = mui;

function mapGeoPartitionSpecToUniverseSpec(
  geoPartitionSpec: ClusterPartitionSpec,
  universeData: Universe
) {
  if (!universeData?.spec) return;

  const primaryCluster = universeData.spec.clusters.find(
    (c) => c.cluster_type === ClusterType.PRIMARY
  );
  if (!primaryCluster) return;

  return {
    spec: {
      ...universeData.spec,
      clusters: universeData.spec.clusters.map((cluster) => ({
        ...cluster,
        placement_spec: geoPartitionSpec.placement
      }))
    },
    arch: universeData.info?.arch
  };
}

export const GeoPartitionReviewAndSummary = () => {
  const [addGeoPartitionContext, addGeoPartitionMethods] = (useContext(
    AddGeoPartitionContext
  ) as unknown) as AddGeoPartitionContextMethods;
  const { geoPartitions, universeData } = addGeoPartitionContext;
  const regions: Region[] = geoPartitions.map((gp) => gp.resilience!.regions).flat();
  const { t } = useTranslation('translation', { keyPrefix: 'geoPartition.reviewAndSummary' });
  const { moveToPreviousPage } = useGeoPartitionNavigation();

  const geoPartitionData = prepareAddGeoPartitionPayload(addGeoPartitionContext);

  const editUniverse = useEditUniverse();

  const costs = useQueries(
    geoPartitions.map((gp) => ({
      queryKey: ['geo-partition-cost', gp.name],
      queryFn: async () => {
        const universeSpec = mapGeoPartitionSpecToUniverseSpec(
          geoPartitionData.find((spec) => spec.name === gp.name)!,
          universeData!
        );
        if (!universeSpec) return;
        return getUniverseResources(universeSpec as any);
      },
      enabled: !!universeData
    }))
  );

  const isCostsLoading = costs.some((result) => result.isLoading);

  if (isCostsLoading) {
    return <YBLoadingCircleIcon />;
  }

  const reviewItems: ReviewItem[] = geoPartitions.map((_, i) => ({
    name: geoPartitions[i].name,
    attributes: [
      { name: 'Nodes', value: costs[i]?.data?.num_nodes ?? '-' },
      { name: 'Cores', value: costs[i]?.data?.num_cores ?? '-' },
      { name: 'Total Memory', value: costs[i]?.data?.mem_size_gb ?? '-' },
      { name: 'Total Storage', value: costs[i]?.data?.volume_size_gb ?? '-' }
    ],
    dailyCost: costs[i]?.data?.price_per_hour
      ? ((costs[i]?.data?.price_per_hour ?? 0) * 24).toFixed(2)
      : '-',
    monthlyCost: costs[i]?.data?.price_per_hour
      ? ((costs[i]?.data?.price_per_hour ?? 0) * 24 * 30).toFixed(2)
      : '-',
    icon: <PinIcon />
  }));

  const totalDailyCost = costs
    .reduce((acc, cost) => acc + (cost.data?.price_per_hour ? cost.data.price_per_hour * 24 : 0), 0)
    .toFixed(2);
  const totalMonthlyCost = costs
    .reduce(
      (acc, cost) => acc + (cost.data?.price_per_hour ? cost.data.price_per_hour * 24 * 30 : 0),
      0
    )
    .toFixed(2);

  const onSubmit = () => {
    if (!universeData?.info || !universeData?.info?.clusters || !universeData.spec) return;
    const payload = prepareAddGeoPartitionPayload(addGeoPartitionContext);
    const primaryCluster = universeData.spec.clusters.find(
      (c) => c.cluster_type === ClusterType.PRIMARY
    );
    if (!primaryCluster) return;

    editUniverse.mutate(
      {
        uniUUID: universeData!.info!.universe_uuid!,
        data: {
          clusters: universeData!.spec!.clusters.map((cluster) => ({
            uuid: cluster!.uuid!,
            partitions_spec: [...(primaryCluster.partitions_spec ?? []), ...payload]
          })),
          expected_universe_version: -1
        }
      },
      {
        onSuccess: () => {
          window.location.href = `/universes/${universeData!.info!.universe_uuid}`;
        },
        onError: (error) => {
          toast.error((error.response?.data as any).error || error.message);
        }
      }
    );
  };

  return (
    <Box sx={{ display: 'flex', flexDirection: 'column', gap: '24px' }}>
      <div style={{ padding: '24px 24px 0 0' }}>
        <GeoPartitionBreadCrumb groupTitle={<>{'Review'}</>} subTitle={<>Summary and Cost</>} />
      </div>
      <ReviewAndSummaryComponent
        regions={regions}
        reviewItems={reviewItems}
        totalDailyCost={totalDailyCost}
        totalMonthlyCost={totalMonthlyCost}
      />
      <UniverseActionButtons
        nextButton={{
          onClick: () => {
            onSubmit();
          },
          text: t('create', { keyPrefix: 'common' })
        }}
        prevButton={{
          onClick: moveToPreviousPage
        }}
      />
    </Box>
  );
};
