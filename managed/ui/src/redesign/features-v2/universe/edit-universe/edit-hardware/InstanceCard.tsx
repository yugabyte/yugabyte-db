import { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { ClusterNodeSpec, ClusterStorageSpec } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { StyledContent, StyledHeader } from './Component';
import { mui, YBButton } from '@yugabyte-ui-library/core';

import { StyledInfoRow } from '../../create-universe/components/DefaultComponents';
import { LinuxVersion } from '../components';

import EditIcon from '@app/redesign/assets/edit2.svg';
interface InstanceCardProps {
  title: string;
  arch?: string;
  nodeSpec?: ClusterNodeSpec;
  storageSpec?: ClusterStorageSpec;
  onEditClicked?: () => void;
}

const { Divider } = mui;

export const InstanceCard: FC<InstanceCardProps> = ({
  title,
  arch,
  nodeSpec,
  storageSpec,
  onEditClicked
}) => {
  const { t } = useTranslation('translation', { keyPrefix: 'editUniverse.hardware' });

  return (
    <StyledContent>
      <StyledHeader>
        <div className="header-title">{title}</div>
        <YBButton
          dataTestId="edit-placement-edit-button"
          variant="ghost"
          startIcon={<EditIcon />}
          onClick={() => onEditClicked && onEditClicked()}
        >
          {t('edit', { keyPrefix: 'common' })}
        </YBButton>
      </StyledHeader>
      {arch && (
        <>
          <StyledInfoRow sx={{ flexDirection: 'row', gap: '90px' }}>
            <div>
              <span className="header">{t('cpuArch', { keyPrefix: 'editUniverse.general' })}</span>
              <span className="value">{arch}</span>
            </div>
            <div>
              <LinuxVersion />
            </div>
          </StyledInfoRow>
          <Divider />
        </>
      )}

      <StyledInfoRow>
        <div>
          <span className="header">{t('instanceType')}</span>
          <span className="value">{nodeSpec?.instance_type ?? '-'}</span>
        </div>
      </StyledInfoRow>
      <StyledInfoRow sx={{ flexDirection: 'row', justifyContent: 'space-between' }}>
        <div>
          <span className="header">{t('volumeAndNode')}</span>
          <span className="value">
            {t('volumeAndNodeValue', {
              volumeSize: storageSpec?.volume_size,
              nodeCount: storageSpec?.num_volumes
            })}
          </span>
        </div>
        <div>
          <span className="header">{t('ebsType')}</span>
          <span className="value">{storageSpec?.storage_type ?? '-'}</span>
        </div>
        <div>
          <span className="header">{t('iops')}</span>
          <span className="value">{storageSpec?.disk_iops ?? '-'}</span>
        </div>
        <div>
          <span className="header">{t('throughput')}</span>
          <span className="value">
            {storageSpec?.throughput
              ? t('throughtputValue', { throughput: storageSpec?.throughput })
              : '-'}
          </span>
        </div>
      </StyledInfoRow>
    </StyledContent>
  );
};
