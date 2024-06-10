import {
  IndexAndShardingRecommendationData,
  RecommendationType
} from '../../../redesign/utils/dtos';
import { isNonEmptyArray } from '../../../utils/ObjectUtils';
import { PerfRecommendationData } from '../../../redesign/utils/dtos';
import { HistogramBucket } from './types';

export const formatPerfRecommendationsData = (perfRecommendations: any) => {
  if (isNonEmptyArray(perfRecommendations?.entities)) {
    // Format data from Range Sharding and Unused Index separately
    const filteredPerfRecommendations = perfRecommendations?.entities?.filter(
      (recommendation: any) =>
        recommendation.recommendationType !== RecommendationType.UNUSED_INDEX &&
        recommendation.recommendationType !== RecommendationType.RANGE_SHARDING &&
        Object.values(RecommendationType).includes(recommendation.recommendationType)
    );

    let recommendations = filteredPerfRecommendations.map((entry: any) => {
      return {
        type: entry.recommendationType,
        observation: entry.observation,
        suggestion: entry.recommendation,
        entityType: entry.entityType,
        target: entry.entityNames,
        recommendationInfo: entry.recommendationInfo,
        recommendationState: entry.recommendationState,
        recommendationPriority: entry.recommendationPriority,
        recommendationTimestamp: entry.recommendationTimestamp,
        isStale: entry.isStale,
        new: entry.new
      };
    });

    // Get consolidated Unused Indexes recommendation here
    const unusedIndexesRecommendation = getIndexesAndShardingRecommendation(
      perfRecommendations,
      RecommendationType.UNUSED_INDEX
    );
    // Get consolidated Range Sharding recommendation here
    const rangeShardingRecommendation = getIndexesAndShardingRecommendation(
      perfRecommendations,
      RecommendationType.RANGE_SHARDING
    );
    recommendations = isNonEmptyArray(rangeShardingRecommendation)
      ? rangeShardingRecommendation.concat(recommendations)
      : recommendations;
    recommendations = isNonEmptyArray(unusedIndexesRecommendation)
      ? unusedIndexesRecommendation.concat(recommendations)
      : recommendations;
    recommendations = recommendations?.sort(
      (a: PerfRecommendationData, b: PerfRecommendationData) => a.type.localeCompare(b.type)
    );
    return recommendations;
  }
  return null;
};

// Returns recommendation for Unused Indexes and Range Sharding
const getIndexesAndShardingRecommendation = (
  perfRecommendations: any,
  type: RecommendationType
): IndexAndShardingRecommendationData[] => {
  const result = [];
  const dbCount: any = getDBCount(perfRecommendations, type);
  for (const key of dbCount.keys()) {
    const indexAndShardingRecommendations = perfRecommendations?.entities?.filter(
      (recommendation: any) =>
        recommendation.recommendationType === type &&
        recommendation.recommendationInfo.database_name === key
    );

    if (indexAndShardingRecommendations.length) {
      result.push({
        type,
        target: key,
        indicator: dbCount.get(key),
        table: {
          data: indexAndShardingRecommendations.sort((a: any, b: any) =>
            a.recommendationInfo.table_name.localeCompare(b.recommendationInfo.table_name)
          )
        }
      });
    }
  }
  return result;
};

// Returns the number of DBs count recommended in case of unused indexes and range sharding
const getDBCount = (perfRecommendations: any, type: RecommendationType) => {
  const unusedIndexDBCount = new Map();
  const rangeShardingDBCount = new Map();

  perfRecommendations?.entities?.map((recommendation: any) => {
    if (recommendation.recommendationType === type) {
      if (unusedIndexDBCount.has(recommendation.recommendationInfo.database_name)) {
        const count = unusedIndexDBCount.get(recommendation.recommendationInfo.database_name);
        unusedIndexDBCount.set(recommendation.recommendationInfo.database_name, count + 1);
      } else {
        unusedIndexDBCount.set(recommendation.recommendationInfo.database_name, 1);
      }
    }

    if (recommendation.recommendationType === type) {
      if (rangeShardingDBCount.has(recommendation.recommendationInfo.database_name)) {
        const count = rangeShardingDBCount.get(recommendation.recommendationInfo.database_name);
        rangeShardingDBCount.set(recommendation.recommendationInfo.database_name, count + 1);
      } else {
        rangeShardingDBCount.set(recommendation.recommendationInfo.database_name, 1);
      }
    }
  });

  return type === RecommendationType.UNUSED_INDEX ? unusedIndexDBCount : rangeShardingDBCount;
};

export const adaptHistogramData = (histogramData: HistogramBucket[]) =>
  histogramData.map((histogramBucket) => ({
    label: Object.keys(histogramBucket)[0],
    value: Object.values(histogramBucket)[0]
  }));
