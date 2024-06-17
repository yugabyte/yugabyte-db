import { lazy, Suspense } from 'react';
import { YBLoadingCircleIcon } from '../components/common/indicators';

const TroubleshootingComponent = lazy(() =>
  import('../redesign/features/Troubleshooting/Troubleshooting').then(({ Troubleshooting }) => ({
    default: Troubleshooting
  }))
);

export const Troubleshoot = (props: any) => {
  return (
    <Suspense fallback={YBLoadingCircleIcon}>
      <TroubleshootingComponent {...props} />
    </Suspense>
  );
};
