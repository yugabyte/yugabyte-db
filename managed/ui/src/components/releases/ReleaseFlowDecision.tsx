import { useSelector } from 'react-redux';
import ReleaseListContainer from './ReleaseList/ReleaseListContainer';
import { NewReleaseList } from '../../redesign/features/releases/components/ReleaseList';
import { RuntimeConfigKey } from '../../redesign/helpers/constants';

export const ReleaseFlowDecision = () => {
  const runtimeConfigs = useSelector((state: any) => state.customer.runtimeConfigs);
  const isReleasesRedesignEnabled =
    runtimeConfigs?.data?.configEntries?.find(
      (c: any) => c.key === RuntimeConfigKey.RELEASES_REDESIGN_UI_FEATURE_FLAG
    )?.value === 'true';

  return isReleasesRedesignEnabled ? <NewReleaseList /> : <ReleaseListContainer />;
};
