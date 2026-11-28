import { useState } from 'react';

import { Universe } from '@app/redesign/features/universe/universe-form/utils/dto';
import { SupportBundle } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

import { CreateSupportBundlePage } from './create/CreateSupportBundlePage';
import { useSupportBundleUniverseContext } from './hooks/useSupportBundleUniverseContext';
import { SupportBundlesModal } from './list/SupportBundlesModal';
import { ShareBundleModal } from './share/ShareBundleModal';

interface SupportBundleV2Props {
  universe: Universe;
  onClose: () => void;
}

/** Entry point for the redesigned support bundle flow, shown behind the redesign flag. */
export const SupportBundleV2 = ({ universe, onClose }: SupportBundleV2Props) => {
  const universeUuid = universe.universeUUID;
  const context = useSupportBundleUniverseContext(universe);
  const [isCreating, setIsCreating] = useState(false);

  const [bundleToShare, setBundleToShare] = useState<SupportBundle | null>(null);

  if (isCreating) {
    return (
      <CreateSupportBundlePage
        universe={universe}
        context={context}
        onClose={() => setIsCreating(false)}
        onCreated={() => setIsCreating(false)}
      />
    );
  }
  return (
    <>
      <SupportBundlesModal
        universeUuid={universeUuid}
        onClose={onClose}
        onCreateBundle={() => setIsCreating(true)}
        onShareBundle={setBundleToShare}
      />
      <ShareBundleModal
        universeUuid={universeUuid}
        bundle={bundleToShare}
        onClose={() => setBundleToShare(null)}
      />
    </>
  );
};
