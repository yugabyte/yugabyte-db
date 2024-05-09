import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { YBErrorIndicator, YBLoading } from '../common/indicators';
import ReleaseListContainer from './ReleaseList/ReleaseListContainer';
import { NewReleaseList } from '../../redesign/features/releases/components/ReleaseList';
import { fetchGlobalRunTimeConfigs } from '../../api/admin';
import { RuntimeConfigKey } from '../../redesign/helpers/constants';

export const ReleaseFlowDecision = () => {
  const { t } = useTranslation();
  const globalRuntimeConfigs = useQuery(['globalRuntimeConfigs'], () =>
    fetchGlobalRunTimeConfigs(true).then((res: any) => res.data)
  );

  if (
    globalRuntimeConfigs.isLoading ||
    (globalRuntimeConfigs.isIdle && globalRuntimeConfigs.data === undefined)
  ) {
    return <YBLoading />;
  }

  if (globalRuntimeConfigs.isError) {
    return (
      <YBErrorIndicator customErrorMessage={t('admin.advanced.globalConfig.GenericConfigError')} />
    );
  }

  const isReleasesRedesignEnabled =
    globalRuntimeConfigs?.data?.configEntries?.find(
      (c: any) => c.key === RuntimeConfigKey.RELEASES_REDESIGN_UI_FEATURE_FLAG
    )?.value === 'true';

  return isReleasesRedesignEnabled ? <NewReleaseList /> : <ReleaseListContainer />;
};
