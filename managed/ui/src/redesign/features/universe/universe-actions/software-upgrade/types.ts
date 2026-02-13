import { YbdbRelease } from './dtos';

export interface ReleaseOption {
  /** Display label for autocomplete (YBAutoComplete expects this for getOptionLabel) */
  label: string;
  version: string;
  releaseInfo: YbdbRelease;
  series: string;
}
