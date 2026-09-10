import { FC } from 'react';
import { useTranslation } from 'react-i18next';
import {
  ClusterNodeSpec,
  ClusterSpec,
  ClusterSpecClusterType,
  ClusterStorageSpec
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import type { K8NodeSpec } from '@app/redesign/features/universe/universe-form/utils/dto';
import { CloudType } from '@app/redesign/helpers/dtos';
import { StyledContent, StyledHeader } from './Component';
import { mui, YBButton, YBTag } from '@yugabyte-ui-library/core';

import { StyledInfoRow } from '../../create-universe/components/DefaultComponents';
import { LinuxVersion } from '../components';

import EditIcon from '@app/redesign/assets/edit2.svg';
import { RbacValidator } from '@app/redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '@app/redesign/features/rbac/ApiAndUserPermMapping';
import {
  getClusterByType,
  useEditUniverseContext,
  useIsUniverseReady,
  withUniverseResource
} from '../EditUniverseUtils';
interface InstanceCardProps {
  title: string;
  arch?: string;
  cluster?: ClusterSpec;
  sameAsPrimaryCluster?: boolean;
  nodeSpec?: ClusterNodeSpec;
  storageSpec?: ClusterStorageSpec;
  isK8s?: boolean;
  k8sResourceSpec?: K8NodeSpec | null;
  onEditClicked?: () => void;
}

const { Divider } = mui;

export const InstanceCard: FC<InstanceCardProps> = ({
  title,
  arch,
  cluster,
  sameAsPrimaryCluster = false,
  nodeSpec,
  storageSpec,
  isK8s = false,
  k8sResourceSpec,
  onEditClicked
}) => {
  const { t } = useTranslation('translation', { keyPrefix: 'editUniverse.hardware' });
  const { universeData } = useEditUniverseContext();
  const universeUUID = universeData?.info?.universe_uuid;
  const isUniverseReady = useIsUniverseReady();
  const primaryCluster = universeData
    ? getClusterByType(universeData, ClusterSpecClusterType.PRIMARY)
    : undefined;
  const providerCode =
    cluster?.placement_spec?.cloud_list?.[0]?.code ??
    primaryCluster?.placement_spec?.cloud_list?.[0]?.code;
  const isAws = providerCode === CloudType.aws;
  return (
    <StyledContent>
      <StyledHeader>
        <div className="header-title">{title}</div>
        <RbacValidator
          accessRequiredOn={withUniverseResource(
            ApiPermissionMap.EDIT_V2_UNIVERSE_PLACEMENT,
            universeUUID
          )}
          isControl
        >
          <YBButton
            dataTestId="edit-placement-edit-button"
            variant="ghost"
            startIcon={<EditIcon />}
            onClick={() => onEditClicked && onEditClicked()}
            disabled={!isUniverseReady}
          >
            {t('edit', { keyPrefix: 'common' })}
          </YBButton>
        </RbacValidator>
      </StyledHeader>
      {arch && (
        <>
          <StyledInfoRow sx={{ flexDirection: 'row', gap: '90px', alignItems: 'center' }}>
            <div>
              <span className="header">{t('cpuArch', { keyPrefix: 'editUniverse.general' })}</span>
              <span className="value sameline">
                {arch}
                {sameAsPrimaryCluster && (
                  <YBTag
                    variant="dark"
                    size="small"
                    customSx={{ color: '#4E5F6D', background: '#E9EEF2' }}
                  >
                    {t('sameAsPrimaryCluster', { keyPrefix: 'editUniverse.general' })}
                  </YBTag>
                )}
              </span>
            </div>
            <div>
              <LinuxVersion cluster={cluster} />
            </div>
          </StyledInfoRow>
          <Divider />
        </>
      )}

      {isK8s && k8sResourceSpec ? (
        <>
          <StyledInfoRow sx={{ flexDirection: 'row', justifyContent: 'space-between' }}>
            <div>
              <span className="header">{t('cpuCores')}</span>
              <span className="value">{k8sResourceSpec.cpuCoreCount ?? '-'}</span>
            </div>
            <div>
              <span className="header">{t('memory')}</span>
              <span className="value">
                {k8sResourceSpec.memoryGib
                  ? t('memoryValue', { memory: k8sResourceSpec.memoryGib })
                  : '-'}
              </span>
            </div>
            <div>
              <span className="header">{t('storageClass')}</span>
              <span className="value">{storageSpec?.storage_class ?? '-'}</span>
            </div>
          </StyledInfoRow>
          <StyledInfoRow sx={{ flexDirection: 'row', justifyContent: 'space-between' }}>
            <div>
              <span className="header">{t('volumeSize')}</span>
              <span className="value">
                {storageSpec?.volume_size
                  ? t('volumeSizeValue', { volumeSize: storageSpec.volume_size })
                  : '-'}
              </span>
            </div>
            <div>
              <span className="header">{t('numVolumes')}</span>
              <span className="value">{storageSpec?.num_volumes ?? '-'}</span>
            </div>
          </StyledInfoRow>
        </>
      ) : (
        <>
          <StyledInfoRow>
            <div>
              <span className="header">{t('instanceType')}</span>
              <span className="value">{nodeSpec?.instance_type ?? '-'}</span>
            </div>
          </StyledInfoRow>
          <StyledInfoRow sx={{ flexDirection: 'row', justifyContent: 'space-between' }}>
            <div>
              <span className="header">{t(isK8s ? 'volumeAndPod' : 'volumeAndNode')}</span>
              <span className="value">
                {t('volumeAndNodeValue', {
                  volumeSize: storageSpec?.volume_size,
                  nodeCount: storageSpec?.num_volumes
                })}
              </span>
            </div>
            <div>
              <span className="header">{t(isAws ? 'ebsType' : 'ssdType')}</span>
              <span className="value">{storageSpec?.storage_type ?? '-'}</span>
            </div>
            <div>
              <span className="header">{t(isK8s ? 'iopsPod' : 'iops')}</span>
              <span className="value">{storageSpec?.disk_iops ?? '-'}</span>
            </div>
            <div>
              <span className="header">{t(isK8s ? 'throughputPod' : 'throughput')}</span>
              <span className="value">
                {storageSpec?.throughput
                  ? t('throughtputValue', { throughput: storageSpec?.throughput })
                  : '-'}
              </span>
            </div>
          </StyledInfoRow>
        </>
      )}
    </StyledContent>
  );
};
