export interface CACertUploadParams {
  name: string;
  contents: string;
}

export interface CACert extends CACertUploadParams {
  id: string;
  name: string;
  active: boolean;
  customerId: string;
  createdTime: string;
  startDate: string;
  expiryDate: string;
}
