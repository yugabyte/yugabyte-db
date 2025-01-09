import axios from 'axios';
import { ROOT_URL } from '../../../config';
import { NodeAgentEntities, ProviderNode } from '../../utils/dtos';

export enum QUERY_KEY {
  fetchNodeAgents = 'fetchNodeAgents',
  fetchOnPremProviderNodeList = 'fetchOnPremProviderNodeList',
  fetchNodeAgentByIPs = 'fetchNodeAgentByIPs'
}

type NodeAgentResponse = {
  entities: NodeAgentEntities[];
  hasNext: boolean;
  hasPrev: boolean;
  totalCount: number;
};

class ApiService {
  private getCustomerId(): string {
    const customerId = localStorage.getItem('customerId');
    return customerId ?? '';
  }

  fetchNodeAgents = () => {
    const requestURL = `${ROOT_URL}/customers/${this.getCustomerId()}/node_agents`;
    return axios.get<NodeAgentEntities[]>(requestURL).then((res) => res.data);
  };

  fetchOnPremProviderNodeList = (providerUUID: string) => {
    const requestURL = `${ROOT_URL}/customers/${this.getCustomerId()}/providers/${providerUUID}/nodes/list`;
    return axios.get<ProviderNode[]>(requestURL).then((res) => res.data);
  };

  fetchNodeAgentByIPs = (payload: any) => {
    const requestURL = `${ROOT_URL}/customers/${this.getCustomerId()}/node_agents/page`;
    return axios.post<NodeAgentResponse>(requestURL, payload).then((res) => res.data);
  };

  deleteNodeAgent = (nodeAgentUuid: string) => {
    const requestURL = `${ROOT_URL}/customers/${this.getCustomerId()}/node_agents/${nodeAgentUuid}`;
    return axios.delete(requestURL).then((res) => res.data);
  };

  installNodeAgent = (universeUuid: string, { nodeNames }: { nodeNames: string[] }) => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/universes/${universeUuid}/node_agents`;
    return axios.post(requestUrl, { nodeNames }).then((response) => response.data);
  };
}

export const NodeAgentAPI = new ApiService();
