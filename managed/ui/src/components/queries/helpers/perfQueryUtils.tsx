
import { 
  fetchUnusedIndexesSuggestions,
  fetchQueryLoadSkewSuggestions,
  fetchRangeShardingSuggestions
} from '../../../actions/universe';
import { RecommendationTypeEnum } from '../../../redesign/helpers/dtos';

export interface QueryData {
  type: RecommendationTypeEnum;
  /** Name of node or database */
  target: string;
  /** Number of other entities used in graph */
  setSize?: number;
  /** Either performance metric or percentage */
  indicator: number;
  /** Time interval this recommendation is for */
  timeDurationSec?: number;
  graph?: {
    max: {
      [column: string]: number;
    };
    other: {
      [column: string]: number;
    };
  };
  table?: {
    data: any[];
  };
  /** UTC timestamp of the start interval, used in certain recommendation types */
  startTime?: string;
  /** UTC timestamp of the end interval, used in certain recommendation types */
  endTime?: string;
}

interface IndexInfoPerDatabase {
  /** Name of the database */
  current_database: string;
  table_name: string;
  index_name: string;
  index_command: string;
}

// TODO: Update API once ready with new endpoint
// Handles Index Suggestions performance advice 
export const handleIndexSuggestionRequest = async(universeUUID: string): Promise<QueryData[] | null> => {
  const result: QueryData[] = [];
    const response = await fetchUnusedIndexesSuggestions(universeUUID);
    if (!response.data?.error && response.data.length) {
      const indexArray: IndexInfoPerDatabase[] = response.data;
      result.push({
        type: RecommendationTypeEnum.IndexSuggestion,
        target: "yugabyte",
        indicator: 4,
        table: {
          data: indexArray.slice(0, 5)
        }
      });
      result.push({
        type: RecommendationTypeEnum.IndexSuggestion,
        target: "employee",
        indicator: 2,
        table: {
          data: indexArray.slice(5, 7)
        }
      });
      return result;
    }
  return null;
};

//TODO: Update API once ready with new endpoint
// Handles Range Sharding performance advice 
export const handleSchemaSuggestionRequest = async(universeUUID: string): Promise<QueryData[] | null> => {
  const response = await fetchRangeShardingSuggestions(universeUUID);
  if (!response.data?.error && response.data.length) {
    const indexArray: IndexInfoPerDatabase[] = response.data;
    return indexArray.map((entry) => ({
      type: RecommendationTypeEnum.SchemaSuggestion,
      target: entry.current_database,
      indicator: indexArray.length,
      table: {
        data: indexArray
      }
    }));
  }
  return null;
}

//TODO: Update API once ready with new endpoint
// Handles the number of queries running on a node  performance advice 
export const handleQuerySkewRequest = async (universeUUID: string): Promise<QueryData | null> => {
  const response = await fetchQueryLoadSkewSuggestions(universeUUID);
  if (!response.data?.error && response.data.length) {
    const maxQueryNodeName = response.data?.node_with_highest_query_load ?? '';
    const maxNodeDetails = response.data?.node_with_highest_query_load_details;
    const highestQueryCount =
      maxNodeDetails.num_select + maxNodeDetails.num_insert + maxNodeDetails.num_update + maxNodeDetails.num_delete;
    const otherNodesDetails = response.data?.other_nodes_average_query_load_details;
    const totalQueryAvg =
      otherNodesDetails.num_select +
      otherNodesDetails.num_insert +
      otherNodesDetails.num_update +
      otherNodesDetails.num_delete;
    const percentDiff = Math.round((100 * (highestQueryCount - totalQueryAvg)) / totalQueryAvg);
    return {
      type: RecommendationTypeEnum.QueryLoadSkew,
      target: maxQueryNodeName,
      setSize: 0,
      indicator: percentDiff,
      graph: {
        max: {
          ...maxNodeDetails
        },
        other: {
          ...otherNodesDetails
        }
      },
      startTime: response.data?.start_time,
      endTime: response.data?.end_time
    };
  }
  return null;
};

// TODO: Add new API endpoints for CPU related measures
