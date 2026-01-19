import { styled } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { NodeAvailabilityProps } from './dtos';
import { StyledContent, StyledHeader, StyledPanel } from '../../components/DefaultComponents';
import { getNodeCount } from '../../CreateUniverseUtils';
import Return from '../../../../../assets/tree.svg';

const StyledContainer = styled('div')(({ theme }) => ({
  border: `1px solid ${theme.palette.grey[300]}`,
  borderRadius: theme.shape.borderRadius,
  padding: theme.spacing(3),
  background: '#FBFCFD',
  width: '672px'
}));
const StyledNodeDetails = styled('div')(({ theme }) => ({
  borderRadius: theme.shape.borderRadius,
  background: '#F7F9FB',
  border: `1px solid ${theme.palette.grey[300]}`,
  padding: `${theme.spacing(1.25)}px ${theme.spacing(2)}px`,
  display: 'flex',
  gap: '10px',
  color: theme.palette.grey[700],
  fontWeight: 600,
  fontSize: '13px'
}));
export const FreeFormRFRequirement = () => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.nodesAndAvailability.freeForm'
  });

  const { watch } = useFormContext<NodeAvailabilityProps>();

  const availabilityZones = watch('availabilityZones');

  const nodesCount = getNodeCount(availabilityZones);

  return (
    <StyledPanel>
      <StyledHeader>{t('nodes')}</StyledHeader>
      <StyledContent>
        <StyledContainer>
          <div style={{ display: 'flex', gap: '8px', alignItems: 'center' }}>
            <Return />
            <StyledNodeDetails>
              <span>{nodesCount}</span>
              <span>{t('totalNodes')}</span>
            </StyledNodeDetails>
          </div>
        </StyledContainer>
      </StyledContent>
    </StyledPanel>
  );
};
