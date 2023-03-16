import { IndexAndShardingRecommendationData, RecommendationType } from '../../../redesign/utils/dtos';
import { isNonEmptyArray } from '../../../utils/ObjectUtils';

export const formatPerfRecommendationsData = (perfRecommendations: any) => {
  if (isNonEmptyArray(perfRecommendations?.entities)) {
    // Format data from Range Sharding and Unused Index separately
    const filteredPerfRecommendations = perfRecommendations?.entities?.filter(
      (recommendation: any) => recommendation.recommendationType !== RecommendationType.UNUSED_INDEX
        && recommendation.recommendationType !== RecommendationType.RANGE_SHARDING
        && Object.values(RecommendationType).includes(recommendation.recommendationType)
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
    const unusedIndexesRecommendation = getIndexesAndShardingRecommendation(perfRecommendations, RecommendationType.UNUSED_INDEX);
    // Get consolidated Range Sharding recommendation here
    const rangeShardingRecommendation = getIndexesAndShardingRecommendation(perfRecommendations, RecommendationType.RANGE_SHARDING);
    recommendations = isNonEmptyArray(rangeShardingRecommendation) ? rangeShardingRecommendation.concat(recommendations) : recommendations;
    recommendations = isNonEmptyArray(unusedIndexesRecommendation) ? unusedIndexesRecommendation.concat(recommendations) : recommendations;

    return recommendations;
  }
  return null;
};

// Returns recommendation for Unused Indexes and Range Sharding
const getIndexesAndShardingRecommendation = (perfRecommendations: any, type: RecommendationType): IndexAndShardingRecommendationData[]  => {
  const result = [];
  const dbCount: any = getDBCount(perfRecommendations);
  for (const key of dbCount.keys()) {
    const unusedIndexRecommendations = perfRecommendations?.entities?.filter((recommendation: any) =>
      recommendation.recommendationType === type && recommendation.recommendationInfo.database_name === key
    );

    if (unusedIndexRecommendations.length) {
      result.push({
        type,
        target: key,
        indicator: dbCount.get(key),
        table: {
          data: unusedIndexRecommendations
        }
      });
    }
  }
  return result;
};

// Returns the number of DBs count recommended in case of unused indexes and range sharding
const getDBCount = (perfRecommendations: any) => {
  const dbCount = new Map();
  perfRecommendations?.entities?.map(
    (recommendation: any) => {
    if (recommendation.recommendationType === RecommendationType.UNUSED_INDEX 
      || recommendation.recommendationType === RecommendationType.RANGE_SHARDING) {
      if (dbCount.has(recommendation.recommendationInfo.database_name)) {
        const count = dbCount.get(recommendation.recommendationInfo.database_name);
        dbCount.set(recommendation.recommendationInfo.database_name, count+1);
      } else {
        dbCount.set(recommendation.recommendationInfo.database_name, 1);
      }
    }
  });
  return dbCount;
};
