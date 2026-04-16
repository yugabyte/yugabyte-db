import { lazy, Suspense } from 'react';
import { YBLoadingCircleIcon } from '../components/common/indicators';

const DrPanelComponent = lazy(() =>
  import('../components/xcluster/disasterRecovery/DrPanel').then(({ DrPanel }) => ({
    default: DrPanel
  }))
);

export const DrPanel = ({ params: { uuid: currentUniverseUuid, drConfigUuid } }: any) => {
  return (
    <Suspense fallback={YBLoadingCircleIcon}>
      <DrPanelComponent currentUniverseUuid={currentUniverseUuid} drConfigUuid={drConfigUuid} />
    </Suspense>
  );
};
