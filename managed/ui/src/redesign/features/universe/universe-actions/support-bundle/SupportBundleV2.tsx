import { SupportBundle } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

import { SupportBundlesModal } from './list/SupportBundlesModal';
import { downloadSupportBundle } from './supportBundleApi';

interface SupportBundleV2Props {
  universeUuid: string;
  onClose: () => void;
}

/** Entry point for the redesigned support bundle flow, shown behind the redesign flag. */
export const SupportBundleV2 = ({ universeUuid, onClose }: SupportBundleV2Props) => {
  const onShareBundle = (bundle: SupportBundle) =>
    downloadSupportBundle(universeUuid, bundle.info?.uuid ?? '', true);

  return (
    <SupportBundlesModal
      universeUuid={universeUuid}
      onClose={onClose}
      onShareBundle={onShareBundle}
    />
  );
};
