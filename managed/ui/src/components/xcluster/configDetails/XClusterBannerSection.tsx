import { YBButton } from '@yugabyte-ui-library/core';
import { useTranslation } from 'react-i18next';

import { hasNecessaryPerm } from '../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../redesign/features/rbac/ApiAndUserPermMapping';
import { XClusterConfigAction } from '../constants';
import { getTableCountsOfConcern } from '../ReplicationUtils';
import {
  XClusterTableWarningBanner,
  XCLUSTER_TABLE_WARNING_BANNER_TEST_ID
} from '../sharedComponents/XClusterTableWarningBanner';
import { useNumTablesAboveLagThreshold } from '../sharedComponents/useNumTablesAboveLagThreshold';

import { XClusterConfig } from '../dtos';
import { Universe } from '../../../redesign/helpers/dtos';

interface XClusterBannerSectionProps {
  sourceUniverse: Universe | undefined;
  xClusterConfig: XClusterConfig;
  enabledConfigActions: XClusterConfigAction[];
  openRestartConfigModal: () => void;
  onViewTables: () => void;
}

export const XClusterBannerSection = ({
  sourceUniverse,
  xClusterConfig,
  enabledConfigActions,
  openRestartConfigModal,
  onViewTables
}: XClusterBannerSectionProps) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'clusterDetail.xCluster.shared.banner'
  });
  const numTablesAboveLagThreshold = useNumTablesAboveLagThreshold(
    sourceUniverse,
    xClusterConfig.uuid
  );

  // Not wrapped in RbacValidator: its <div> wrapper is invalid inside the banner description <p>.
  const canRestart =
    enabledConfigActions.includes(XClusterConfigAction.RESTART) &&
    hasNecessaryPerm({
      ...ApiPermissionMap.MODIFY_XCLUSTER_REPLICATION,
      onResource: xClusterConfig.sourceUniverseUUID
    }) &&
    hasNecessaryPerm({
      ...ApiPermissionMap.MODIFY_XCLUSTER_REPLICATION,
      onResource: xClusterConfig.targetUniverseUUID
    });
  const xClusterConfigTables = xClusterConfig.tableDetails ?? [];
  return (
    <XClusterTableWarningBanner
      tableCountsOfConcern={getTableCountsOfConcern(xClusterConfigTables)}
      numTablesAboveLagThreshold={xClusterConfigTables.length > 0 ? numTablesAboveLagThreshold : 0}
      isDrInterface={false}
      onViewTables={onViewTables}
      restartButton={
        <YBButton
          variant="secondary"
          size="small"
          onClick={openRestartConfigModal}
          disabled={!canRestart}
          dataTestId={`${XCLUSTER_TABLE_WARNING_BANNER_TEST_ID}-restart`}
        >
          {t('restartReplication')}
        </YBButton>
      }
    />
  );
};
