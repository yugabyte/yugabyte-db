import { AZ_NOT_PREFERRED, AZ_PREFFERED_HIGHEST_RANK } from '../create-universe/helpers/constants';

type AzLeaderFields = { leader_affinity?: boolean; leader_preference?: number };

/**
 * Rank of each AZ as the backend applies it: once any AZ has leader_preference > 0 the ranks decide
 * (UpdatePlacementInfo only sets preferred zones for rank > 0) and leader_affinity is ignored.
 * Without ranks, an AZ counts as preferred only if another AZ was excluded (leader_affinity false);
 * when every AZ is affinitized nothing is configured and no AZ is shown as preferred.
 * `azs` must be all AZs of one cluster's placement.
 */
export const getAzRankGetter = (azs: AzLeaderFields[]) => {
  const hasRanks = azs.some((az) => (az.leader_preference ?? 0) > 0);
  const anyExcluded = azs.some((az) => az.leader_affinity === false);
  return (az: AzLeaderFields): number => {
    if (hasRanks) return az.leader_preference ?? AZ_NOT_PREFERRED;
    return anyExcluded && az.leader_affinity !== false
      ? AZ_PREFFERED_HIGHEST_RANK
      : AZ_NOT_PREFERRED;
  };
};

type MapRegion = { zones?: AzLeaderFields[]; clusterType?: string };

/** Rank getter for map views, whose regions mix primary and read replica zones. Leader preference applies to primary only. */
export const getMapRankGetter = (regions: MapRegion[] | undefined) => {
  const isReadReplica = (region: MapRegion) => region.clusterType === 'ASYNC';
  const rankOf = getAzRankGetter(
    (regions ?? []).filter((r) => !isReadReplica(r)).flatMap((r) => r.zones ?? [])
  );
  return (region: MapRegion, az: AzLeaderFields) => (isReadReplica(region) ? 0 : rankOf(az));
};
