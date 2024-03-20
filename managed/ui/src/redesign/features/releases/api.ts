import axios from 'axios';
import { ROOT_URL } from '../../../config';
import { ReleaseArtifactId, ReleaseSpecificArtifact, Releases, UUID } from './components/dtos';

class ApiService {
  private getCustomerId(): string {
    const customerId = localStorage.getItem('customerId');
    return customerId ?? '';
  }

  // Fetches list of all releases
  fetchReleasesList = () => {
    const requestURL = `${ROOT_URL}/customers/${this.getCustomerId()}/releases/list`;
    return axios.get<Releases[]>(requestURL).then((res) => res.data);
  };

  // Fetches a specific release
  fetchRelease = (releaseUUID: string) => {
    const requestURL = `${ROOT_URL}/customers/${this.getCustomerId()}/releases/${releaseUUID}`;
    return axios.get<Releases>(requestURL).then((res) => res.data);
  };

  // Fetches a release's file artifact (File Upload)
  fetchFileReleaseArtifact = (fileID: string) => {
    const requestURL = `${ROOT_URL}/customers/${this.getCustomerId()}/releases/upload/${fileID}`;
    return axios.get<ReleaseSpecificArtifact>(requestURL).then((res) => res.data);
  };

  // Fetches a release's specific artifact (URL)
  fetchUrlReleaseArtifact = (url: string) => {
    const requestURL = `${ROOT_URL}/customers/${this.getCustomerId()}/releases/extract_metadata`;
    return axios.get<ReleaseSpecificArtifact>(requestURL,  {
        params: {
          url
        }
      }).then((res) => res.data);
  };

  // Create a new release
  createRelease = (data: Releases) => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/releases`;
    return axios.post<UUID>(requestUrl, data).then((resp) => resp.data);
  };

  // Upload a new release artifact by streaming the file bits
  uploadReleaseArtifact = (fileContent: any) => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/releases/upload`;
    return axios.post<ReleaseArtifactId>(requestUrl, fileContent).then((resp) => resp.data);
  };

  // Edit/Update a specific release, operations like adding/remove artifacts are performed
  updateReleaseMetadata = (data: Releases, releaseUUID: string) => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/releases/${releaseUUID}`;
    return axios.put(requestUrl, data).then((resp) => resp.data);
  };

  // Delete a specific release
  deleteRelease = (releaseUUID: string) => {
    const requestUrl = `${ROOT_URL}/customers/${this.getCustomerId()}/releases/${releaseUUID}`;
    return axios.delete(requestUrl);
  };
}

export const ReleasesAPI = new ApiService();
