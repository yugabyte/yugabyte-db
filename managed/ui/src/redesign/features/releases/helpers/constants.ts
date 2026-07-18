
import { AddReleaseImportMethod } from "../components/dtos";

export const IMPORT_METHOD_OPTIONS = [
  {
    value: AddReleaseImportMethod.FILE_UPLOAD,
    label: 'File Upload'
  },
  {
    value: AddReleaseImportMethod.URL,
    label: 'URL'
  }
];

export const REFETCH_URL_METADATA_MS = 3_000; // 3 seconds
export const FILE_SIZE_LIMIT = 1048576000; // 1000 MB
