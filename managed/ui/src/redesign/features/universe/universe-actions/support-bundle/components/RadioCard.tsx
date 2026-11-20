import { ReactNode } from 'react';
import { mui } from '@yugabyte-ui-library/core';

import CircleCheckSolidIcon from '@app/redesign/assets/approved/circle-check-solid.svg';
import CardTriangleControlIcon from '@app/redesign/assets/approved/card-triangle-control.svg';

const { styled, Box, Typography, ButtonBase } = mui;

const Card = styled(Box, { shouldForwardProp: (prop) => prop !== 'selected' })<{
  selected: boolean;
}>(({ theme, selected }) => ({
  display: 'flex',
  flexDirection: 'column',
  gap: theme.spacing(2.5),
  padding: theme.spacing(2.5),
  borderRadius: 8,
  border: `1px solid ${selected ? theme.palette.primary[400] : theme.palette.grey[300]}`,
  background: selected ? theme.palette.primary[100] : theme.palette.common.white
}));

const Header = styled(ButtonBase)({
  display: 'flex',
  alignItems: 'center',
  justifyContent: 'space-between',
  width: '100%',
  textAlign: 'left'
});

const UnselectedRadio = styled('span')(({ theme }) => ({
  width: 20,
  height: 20,
  margin: 2,
  borderRadius: '50%',
  border: `1px solid ${theme.palette.grey[400]}`,
  background: theme.palette.common.white
}));

interface RadioCardProps {
  title: string;
  description: string;
  selected: boolean;
  expanded: boolean;
  disabled?: boolean;
  onSelect: () => void;
  onToggleExpanded: () => void;
  dataTestId: string;
  children?: ReactNode;
}

/**
 * A bordered card with a radio-style marker. Selecting a card expands it; the caret only
 * expands or collapses, so an unselected card can be opened without changing the selection.
 */
export const RadioCard = ({
  title,
  description,
  selected,
  expanded,
  disabled,
  onSelect,
  onToggleExpanded,
  dataTestId,
  children
}: RadioCardProps) => (
  <Card selected={selected} data-testid={dataTestId}>
    <Box display="flex" alignItems="center" gap={1.5}>
      <Header
        role="radio"
        aria-checked={selected}
        disabled={disabled}
        onClick={onSelect}
        data-testid={`${dataTestId}-Select`}
      >
        <Box display="flex" alignItems="center" gap={1.5}>
          <Box display="flex" color="primary.600">
            {selected ? <CircleCheckSolidIcon /> : <UnselectedRadio />}
          </Box>
          <Box display="flex" flexDirection="column" gap={0.25}>
            <Typography variant="body1" fontWeight={600} color="text.primary">
              {title}
            </Typography>
            <Typography variant="subtitle1" color="grey.700">
              {description}
            </Typography>
          </Box>
        </Box>
      </Header>
      <ButtonBase
        aria-expanded={expanded}
        aria-label={title}
        disabled={disabled}
        onClick={onToggleExpanded}
        data-testid={`${dataTestId}-Expand`}
        sx={{ display: 'flex', transform: expanded ? undefined : 'rotate(180deg)' }}
      >
        <CardTriangleControlIcon />
      </ButtonBase>
    </Box>
    {expanded && children}
  </Card>
);
