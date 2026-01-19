import { mui, YBToggleField } from '@yugabyte-ui-library/core';
import { useContext } from 'react';
import { StyledProps, Typography } from '@material-ui/core';
import { Trans, useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { StyledPanel } from '../../components/DefaultComponents';
import { NodeAvailabilityProps } from './dtos';
import { getNodeCount } from '../../CreateUniverseUtils';
import { CreateUniverseContext, CreateUniverseContextMethods } from '../../CreateUniverseContext';
import { ArrowDropDown } from '@material-ui/icons';
import Return from '../../../../../assets/tree.svg';

const { styled } = mui;

const { Accordion, AccordionSummary, AccordionDetails } = mui;

const DedicatedNodeHelpText = styled('div')(({ theme }) => ({
  marginLeft: '44px',
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
          <div>{t('helpText')}</div>
          <div>
            <Trans t={t} i18nKey={'helpTextLink'} components={{ b: <b />, a: <a href="#" /> }} />
          </div>
        </DedicatedNodeHelpText>
      </StyledToggleArea>
      <div style={{ marginLeft: '44px', paddingBottom: '24px' }}>
        {useDedicatedNodes && (
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
                marginLeft: '24px'
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
              </div>
            </div>
          </>
        )}
      </div>
    </>
  );

  if (noAccordion) {
    return getStyledToggleArea();
  }

  return (
    <StyledPanel>
      <Accordion>
        <AccordionSummary
          expandIcon={<ArrowDropDown style={{ fontSize: '24px', color: 'black' }} />}
        >
          <Typography variant="h5" style={{ fontWeight: 600 }}>
            {t('title')}
          </Typography>
        </AccordionSummary>
        <AccordionDetails>
          <StyledPanel>{getStyledToggleArea()}</StyledPanel>
        </AccordionDetails>
      </Accordion>
    </StyledPanel>
  );
};
