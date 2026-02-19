import { useContext } from 'react';
import { useFormContext } from 'react-hook-form';
import { Trans, useTranslation } from 'react-i18next';
import { StyledProps } from '@material-ui/core';
import { mui, YBToggleField, YBAccordion, YBTooltip } from '@yugabyte-ui-library/core';
import { StyledPanel } from '../../components/DefaultComponents';
import { NodeAvailabilityProps } from './dtos';
import { getNodeCount } from '../../CreateUniverseUtils';
import { CreateUniverseContext, CreateUniverseContextMethods } from '../../CreateUniverseContext';

//icons
import Return from '../../../../../assets/tree.svg';
import InfoIcon from '../../../../../assets/info-new.svg';

const { styled, Link } = mui;

const DedicatedNodeHelpText = styled('div')(({ theme }) => ({
  marginTop: '4px',
  marginLeft: '48px',
  color: theme.palette.grey[700]
}));

const StyledToggleArea = styled('div')(({ theme }) => ({
  padding: '24px',
  '& input[name="useDedicatedNodes"]': {
    margin: 0
  }
}));

const NodesCount = styled('span', {
  shouldForwardProp: (prop) => prop !== 'header'
})<StyledProps>(({ theme, header }) => ({
  padding: '12px 16px',
  background: '#F7F9FB',
  border: `1px solid ${theme.palette.grey[300]}`,
  borderRadius: '8px',
  display: 'flex',
  gap: '10px',
  width: 'fit-content',
  fontWeight: header ? 600 : 500,
  fontSize: '13px',
  color: header ? theme.palette.grey[700] : theme.palette.grey[600]
}));

export const DedicatedNode = ({ noAccordion }: { noAccordion?: boolean }) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.nodesAndAvailability.dedicatedNodes'
  });
  const { control, watch } = useFormContext<NodeAvailabilityProps>();
  const [{ resilienceAndRegionsSettings }] = (useContext(
    CreateUniverseContext
  ) as unknown) as CreateUniverseContextMethods;

  const availabilityZones = watch('availabilityZones');
  const useDedicatedNodes = watch('useDedicatedNodes');
  const nodeCount = getNodeCount(availabilityZones);

  const getStyledToggleArea = () => (
    <>
      <StyledToggleArea>
        <YBToggleField
          label={t('toggle')}
          control={control}
          name="useDedicatedNodes"
          inputProps={{ margin: 0 }}
          dataTestId="use-dedicated-nodes-field"
        />
        <DedicatedNodeHelpText>
          <div>{t('helpText1')}</div>
          <div>
            <Trans t={t} i18nKey={'helpText2'} components={{ b: <b />, a: <a href="#" /> }} />
          </div>
        </DedicatedNodeHelpText>
      </StyledToggleArea>
      {useDedicatedNodes && (
        <div style={{ paddingBottom: '24px', paddingLeft: '38px' }}>
          <>
            <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
              <Return />
              <NodesCount header>
                <span>{nodeCount + resilienceAndRegionsSettings!.replicationFactor}</span>
                <span>{t('totalNodes')}</span>
              </NodesCount>
            </div>
            <div
              style={{
                display: 'flex',
                gap: '16px',
                marginTop: '8px',
                alignItems: 'center',
                marginLeft: '32px'
              }}
            >
              <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
                <NodesCount>
                  <span>{nodeCount}</span>
                </NodesCount>
                <span style={{ color: '#6D7C88' }}>{t('totalTServer')}</span>
              </div>
              <span style={{ fontSize: '15px', fontWeight: '500' }}>+</span>
              <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
                <NodesCount>
                  <span>{resilienceAndRegionsSettings?.replicationFactor}</span>
                </NodesCount>
                <span style={{ color: '#6D7C88' }}>{t('totalMaster')}</span>
                <YBTooltip
                  title={
                    <Trans
                      t={t}
                      style={{ fontSize: '11.5px', fontWeight: 400, lineHeight: '16px' }}
                      i18nKey="tooltip"
                      components={{
                        a: <Link />,
                        br: <br />
                      }}
                    />
                  }
                >
                  <span style={{ marginTop: '6px', marginLeft: '-4px', cursor: 'pointer' }}>
                    <InfoIcon />
                  </span>
                </YBTooltip>
              </div>
            </div>
          </>
        </div>
      )}
    </>
  );

  if (noAccordion) {
    return getStyledToggleArea();
  }

  return (
    <YBAccordion titleContent={t('title')} sx={{ width: '100%' }}>
      <StyledPanel>{getStyledToggleArea()}</StyledPanel>
    </YBAccordion>
  );
};
