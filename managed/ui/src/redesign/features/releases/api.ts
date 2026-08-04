import axios from 'axios';
import { ROOT_URL } from '../../../config';
import { ReleaseSpecificArtifact, Releases, UUID } from './components/dtos';

export enum QUERY_KEY {
  fetchReleasesList = 'fetchReleasesList',
  fetchRelease = 'fetchRelease',
  fetchFileReleaseArtifact = 'fetchFileReleaseArtifact',
  fetchUrlReleaseArtifact = 'fetchUrlReleaseArtifact',
  createRelease = 'createRelease',
  refreshRelease = 'refreshRelease',
  uploadReleaseArtifact = 'uploadReleaseArtifact',
  updateReleaseMetadata = 'updateReleaseMetadata',
  deleteRelease = 'deleteRelease'
}

export const AXIOS_INSTANCE = axios.create({ baseURL: ROOT_URL, withCredentials: true });
export const releaseArtifactKey = {
  ALL: ['releaseArtifact'],
  resource: (resourceUuid: string) => [...releaseArtifactKey.ALL, 'resource', resourceUuid],
  file: (fileId: string) => [...releaseArtifactKey.ALL, 'file', fileId]
};

class ApiService {
  private getCustomerId(): string {
    const customerId = localStorage.getItem('customerId');
    return customerId ?? '';
  }

  // Fetches list of all releases
  fetchReleasesList = () => {
    const requestURL = `${ROOT_URL}/customers/${this.getCustomerId()}/ybdb_release`;
    return axios.get<Releases[]>(requestURL).then((res) => res.data);
  };

  // Fetches a specific release
  fetchRelease = (releaseUUID: string) => {
    const requestURL = `${ROOT_URL}/customers/${this.getCustomerId()}/ybdb_release/${releaseUUID}`;
    return axios.get<Releases>(requestURL).then((res) => res.data);
  };

  // Fetches a release's file artifact (File Upload)
  fetchFileReleaseArtifact = (resourceUuid: string) => {
    const requestURL = `${ROOT_URL}/customers/${this.getCustomerId()}/ybdb_release/upload/${resourceUuid}`;
    return axios.get<ReleaseSpecificArtifact>(requestURL).then((res) => res.data);
  };

  // Extract release metadata
  extractReleaseArtifact = (artifactUrl: string) => {
    const requestURL = `${ROOT_URL}/customers/${this.getCustomerId()}/ybdb_release/extract_metadata`;
    return axios.post<ReleaseSpecificArtifact>(requestURL,  {
      url: artifactUrl
    }).then((res) => res.data);
  };

  // Fetches a release's specific artifact (URL)
  getUrlReleaseArtifact = (resourceUuid: string, ) => {
    const requestURL = `${ROOT_URL}/customers/${this.getCustomerId()}/ybdb_release/extract_metadata/${resourceUuid}`;
    return axios.get<ReleaseSpecificArtifact>(requestURL).then((resp) => resp.data);
  };

  // Create a new release
  createRelease = (data: Releases) => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/ybdb_release`;
    return axios.post<UUID>(requestUrl, data).then((resp) => resp.data);
  };

  refreshRelease = () => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/releases`;
    return axios.put(requestUrl).then((resp) => resp.data);
  };

  // Edit/Update a specific release, operations like adding/remove artifacts are performed
  updateReleaseMetadata = (data: Releases, releaseUUID: string) => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/ybdb_release/${releaseUUID}`;
    return axios.put(requestUrl, data).then((resp) => resp.data);
  };

  // Delete a specific release
  deleteRelease = (releaseUUID: string) => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/ybdb_release/${releaseUUID}`;
    return axios.delete(requestUrl);
  };
}

export const ReleasesAPI = new ApiService();
