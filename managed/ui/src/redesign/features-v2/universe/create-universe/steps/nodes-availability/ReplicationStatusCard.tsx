import { useCallback, useContext, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { mui } from '@yugabyte-ui-library/core';
import { ReplicationFactorInfoModal } from './index';
import { CreateUniverseContext, CreateUniverseContextMethods } from '../../CreateUniverseContext';
import { getFaultToleranceNeededForAZ } from '../../CreateUniverseUtils';
import { FaultToleranceType } from '../resilence-regions/dtos';

//icons
import { HelpOutline } from '@material-ui/icons';
import Check from '../../../../../assets/check-grey.svg';
import NotAllowed from '../../../../../assets/revoke.svg';
import DocTick from '../../../../../assets/doc_tick_purple.svg';

const { List, ListItem, styled, Divider, Typography } = mui;

const StyledReplicationCardHeader = styled('div')(({ theme }) => ({
  background: '#F2F3FE',
  borderRadius: '8px 8px 0px 0px',
  borderTop: `1px solid ${theme.palette.grey[300]}`,
  borderLeft: `1px solid ${theme.palette.grey[300]}`,
  borderRight: `1px solid ${theme.palette.grey[300]}`,
  padding: '16px',
  display: 'flex',
  alignItems: 'center',
  gap: '8px',
  color: theme.palette.secondary[600],
  fontWeight: 500,
  fontSize: '11.5px'
}));

const StyledReplicationCardContent = styled('div')(({ theme }) => ({
  background: '#F2F3FE',
  borderRadius: '0px 0px 8px 8px',
  border: `1px solid ${theme.palette.grey[300]}`,
  padding: '8px 16px 16px 32px',
  display: 'flex',
  alignItems: 'center',
  gap: '8px'
}));

const CustomList = styled(List)(({ theme }) => ({
  padding: 0,
  margin: 0,
  listStyle: 'none'
}));

const CustomListItem = styled(ListItem)(({ theme }) => ({
  paddingLeft: '10px 0px',
  color: theme.palette.grey[700],
  fontSize: '11.5px',
  fontWeight: 500,
  '&::before': {
    content: '""',
    position: 'absolute',
    left: 0,
    top: '50%',
    transform: 'translateY(-50%)',
    width: '8px',
    height: '8px',
    backgroundColor: '#D7DEE4',
    borderRadius: '50%'
  }
}));

const StyledFaultToleranceType = styled('span')(({ theme }) => ({
  padding: '4px 6px',
  borderRadius: '6px',
  border: `1px solid ${theme.palette.grey[300]}`,
  background: theme.palette.common.white,
  marginLeft: '8px'
}));

export const ReplicationStatusCard = ({ hideSubText = false }: { hideSubText?: boolean }) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.nodesAndAvailability.replicationFactorCard'
  });
  const [{ resilienceAndRegionsSettings }] = (useContext(
    CreateUniverseContext
  ) as unknown) as CreateUniverseContextMethods;
  const [showResilienceTooltip, setShowResilienceTooltip] = useState(false);

  const { watch } = useFormContext();
  const replicationFactor =
    watch('replicationFactor') || resilienceAndRegionsSettings?.replicationFactor;
  const faultToleranceType =
    watch('faultToleranceType') || resilienceAndRegionsSettings?.faultToleranceType;

  const acrossLabel = useCallback(() => {
    switch (faultToleranceType) {
      case FaultToleranceType.AZ_LEVEL:
        return t('acrossAvailabilityZone');
      case FaultToleranceType.REGION_LEVEL:
        return t('acrossRegions');
      case FaultToleranceType.NODE_LEVEL:
        return t('acrossNodes');
      case FaultToleranceType.NONE:
        return t('acrossNone');
      default:
        return null;
    }
  }, [faultToleranceType, t]);

  const nodeCount =
    faultToleranceType === FaultToleranceType.NONE
      ? 1
      : getFaultToleranceNeededForAZ(replicationFactor);
  return (
    <div>
      <StyledReplicationCardHeader>
        <DocTick />
        {t('title')}
        {!hideSubText && (
          <>
            <Divider
              orientation="vertical"
              flexItem
              style={{ margin: '0 8px', backgroundColor: '#D7DEE4' }}
            />
            {t(`${faultToleranceType}.subText`, {
              keyPrefix: 'createUniverseV2.resilienceAndRegions.guidedMode'
            })}
          </>
        )}
      </StyledReplicationCardHeader>
      <StyledReplicationCardContent>
        <CustomList>
          <CustomListItem>
            {t(faultToleranceType === FaultToleranceType.NONE ? 'nodeMinumum' : 'minumum', {
              total_nodes: nodeCount
            })}
          </CustomListItem>
          <CustomListItem>
            {t(faultToleranceType === FaultToleranceType.NONE ? 'within' : 'across')}
            <StyledFaultToleranceType>
              {nodeCount}&nbsp;{acrossLabel()}
            </StyledFaultToleranceType>
          </CustomListItem>
          <CustomListItem style={{ display: 'flex', alignItems: 'center', gap: '4px' }}>
            {t('replicationFactor', { replication_factor: nodeCount })}
            <HelpOutline
              onClick={() => {
                setShowResilienceTooltip(true);
              }}
              style={{ cursor: 'pointer' }}
            />
          </CustomListItem>
        </CustomList>
      </StyledReplicationCardContent>
      <ReplicationFactorInfoModal
        open={showResilienceTooltip}
        onClose={() => {
          setShowResilienceTooltip(false);
        }}
      />
    </div>
  );
};

const StyledTypography = styled(Typography)(({ theme }) => ({
  color: theme.palette.grey[700],
  lineHeight: '20px'
}));

export const ReplicationStatusAvailabilityStatus = () => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.nodesAndAvailability.replicationFactorCard'
  });
  const { watch } = useFormContext();
  const replicationFactor = watch('replicationFactor');
  const faultToleranceType = watch('faultToleranceType');
  return (
    <div style={{ display: 'flex', alignItems: 'center', gap: '16px' }}>
      {faultToleranceType !== FaultToleranceType.NONE && (
        <div style={{ display: 'flex', alignItems: 'center' }}>
          <Check />
          <StyledTypography variant="subtitle1">
            {t('nodeOutage', {
              node_count:
                faultToleranceType !== FaultToleranceType.NODE_LEVEL ? '' : replicationFactor
            })}
          </StyledTypography>
        </div>
      )}

      {![FaultToleranceType.NODE_LEVEL, FaultToleranceType.NONE].includes(faultToleranceType) && (
        <div style={{ display: 'flex', alignItems: 'center' }}>
          <Check />
          <StyledTypography variant="subtitle1">
            {t('azOutage', {
              node_count:
                faultToleranceType !== FaultToleranceType.AZ_LEVEL ? '' : replicationFactor
            })}
          </StyledTypography>
        </div>
      )}
      {faultToleranceType === FaultToleranceType.REGION_LEVEL && (
        <div style={{ display: 'flex', alignItems: 'center' }}>
          <Check />
          <StyledTypography variant="subtitle1">
            {t('regionOutage', { node_count: replicationFactor })}
          </StyledTypography>
        </div>
      )}
      {faultToleranceType === FaultToleranceType.NONE && (
        <div style={{ display: 'flex', alignItems: 'center' }}>
          <NotAllowed />
          <StyledTypography variant="subtitle1">
            {t('nodeNotAvailable', { node_count: replicationFactor })}
          </StyledTypography>
        </div>
      )}
      {[FaultToleranceType.NODE_LEVEL, FaultToleranceType.NONE].includes(faultToleranceType) && (
        <>
          <div style={{ display: 'flex', alignItems: 'center' }}>
            <NotAllowed />
            <StyledTypography variant="subtitle1">{t('azNotAvailable')}</StyledTypography>
          </div>
          <div style={{ display: 'flex', alignItems: 'center' }}>
            <NotAllowed />
            <StyledTypography variant="subtitle1">{t('regionNotAvailable')}</StyledTypography>
          </div>
        </>
      )}
    </div>
  );
};
