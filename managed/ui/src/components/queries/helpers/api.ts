import axios from 'axios';
import { ROOT_URL } from '../../../config';
import { RecommendationPriority, SortDirection } from '../../../redesign/utils/dtos';

export enum QUERY_KEY {
  fetchPerfRecommendations = 'fetchPerfRecommendations'
}

interface PerfRecommendationQueryFilter {
  universeId: string;
  types?: string[];
  priorities?: RecommendationPriority[];
  isStale?: boolean
}

interface PerfRecommendationQueryParams {
  filter: PerfRecommendationQueryFilter;
  sortBy?: string;
  direction: SortDirection;
  offset: number;
  limit: number;
  needTotalCount?: boolean
}

class ApiService {
  private getCustomerId(): string {
    const customerId = localStorage.getItem('customerId');
    return customerId ?? '';
  }

  fetchPerfRecommendationsList = (data: PerfRecommendationQueryParams): Promise<any[]> => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/performance_recommendations/page`;
    return axios.post(requestUrl, data).then((resp) => resp.data);
  };
}

export const performanceRecommendationApi = new ApiService();
