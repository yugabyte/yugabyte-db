import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { useDispatch } from 'react-redux';
import { openDialog } from '@app/actions/modal';
import { YBButton } from '@yugabyte-ui-library/core';
import { fetchProviderList } from '@app/api/admin';
import {
  ImageBundle,
  ImageBundleType
} from '@app/redesign/features/universe/universe-form/utils/dto';
import { ClusterSpec, ClusterSpecClusterType } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { useEditUniverseContext, getClusterByType } from '../EditUniverseUtils';
import { ImageBundleDefaultTag, ImageBundleYBActiveTag } from '../../create-universe/fields';
import { AWSProvider } from '@app/components/configRedesign/providerRedesign/types';
import { CircularProgress } from '@material-ui/core';
import { StyledInfoRow } from '../../create-universe/components/DefaultComponents';
import ArrowCircleUp from '@app/redesign/assets/arrow_circle_up.svg';

export const LinuxVersion = ({ cluster }: { cluster?: ClusterSpec }) => {
  const { universeData } = useEditUniverseContext();
  const { t } = useTranslation('translation', { keyPrefix: 'editUniverse.general' });
  const targetCluster = cluster ?? getClusterByType(universeData!, ClusterSpecClusterType.PRIMARY);

  const dispatch = useDispatch();

  const { data: providers, isLoading: isProvidersLoading } = useQuery(
    ['providers'],
    () => fetchProviderList(),
    { select: (data) => data.data }
  );

  const currentProvider = providers?.find(
    (provider) => provider.uuid === targetCluster?.provider_spec.provider
  );

  const imageBundleUsed: ImageBundle | undefined = (
    currentProvider as AWSProvider
  )?.imageBundles?.find(
    (imgBundle: any) => imgBundle?.uuid === targetCluster?.provider_spec?.image_bundle_uuid
  ) as unknown as ImageBundle;

  return (
    <StyledInfoRow>
      <div>
        <span className="header">{t('linuxVersion')}</span>
        <span className="value sameline">
          {isProvidersLoading && <CircularProgress size="20px" color="secondary" />}
          {imageBundleUsed?.name ?? '-'}
          {imageBundleUsed?.metadata?.type === ImageBundleType.YBA_ACTIVE && (
            <ImageBundleYBActiveTag />
          )}
          {imageBundleUsed?.metadata?.type === ImageBundleType.YBA_DEPRECATED && (
            <ImageBundleDefaultTag
              text={t('retired', { keyPrefix: 'linuxVersion.upgradeModal' })}
              icon={<></>}
              tooltip={t('retiredTooltip', { keyPrefix: 'linuxVersion.upgradeModal' })}
            />
          )}
          {imageBundleUsed?.metadata?.type === ImageBundleType.YBA_DEPRECATED && (
            <YBButton
              startIcon={<ArrowCircleUp />}
              dataTestId="upgrade-linux-version"
              variant="ghost"
              onClick={() => {
                dispatch(openDialog('linuxVersionUpgradeModal'));
              }}
            >
              {t('upgrade', { keyPrefix: 'common' })}
            </YBButton>
          )}
        </span>
      </div>
    </StyledInfoRow>
  );
};
