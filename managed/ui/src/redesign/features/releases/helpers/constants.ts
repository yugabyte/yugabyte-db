
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
