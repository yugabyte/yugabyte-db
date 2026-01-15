import { useMemo } from 'react';
import { styled, Typography } from '@material-ui/core';
import { Controller, FieldValues, Path, useFormContext } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { ResilienceType } from '../../steps/resilence-regions/dtos';

import Empty from '../../../../../assets/circle-unselected.svg';
import Selected from '../../../../../assets/circle-checked.svg';

const StyledContainer = styled('div')({
  display: 'flex',
  flexDirection: 'row',
  gap: '8px'
});

const StyledCard = styled('div')(({ theme }) => ({
  display: 'flex',
  padding: '16px',
  borderRadius: '8px',
  //
  border: `1px solid ${theme.palette.primary[300]}`,
  width: '330px',
  height: '56px',
  color: theme.palette.grey[900],
  fontSize: '13px',
  fontWeight: 600,
  alignItems: 'center',
  justifyContent: 'space-between',
  cursor: 'pointer',
  '&.active': {
    backgroundColor: theme.palette.primary[100]
  }
}));

interface ResilienceTypeProps<T> {
  name: Path<T>;
}

const Info = styled('span')(({ theme }) => ({
  padding: '4px 6px',
  borderRadius: '6px',
  background: 'transparent',
  border: `1px solid ${theme.palette.grey[300]}`
}));

export const ResilienceTypeField = <T extends FieldValues>({ name }: ResilienceTypeProps<T>) => {
  const { control } = useFormContext<T>();
  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.resilienceAndRegions.resilienceType'
  });

  const items = useMemo(
    () => [
      {
        label: t('regular'),
        value: ResilienceType.REGULAR
      },
      {
        label: t('singleNode'),
        value: ResilienceType.SINGLE_NODE,
        subText: t('quickSetup')
      }
    ],
    []
  );
  return (
    <Controller
      control={control}
      name={name}
      render={({ field }) => (
        <StyledContainer>
          {items.map((item) => (
            <StyledCard
              key={item.value}
              onClick={() => {
                field.onChange(item.value);
              }}
              className={`${field.value === item.value ? 'active' : ''}`}
            >
              {item.subText ? (
                <span style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
                  {item.label}
                  <Info>
                    <Typography variant="subtitle1">{item.subText}</Typography>
                  </Info>
                </span>
              ) : (
                item.label
              )}
              {field.value === item.value ? <Selected /> : <Empty />}
            </StyledCard>
          ))}
        </StyledContainer>
      )}
    />
  );
};
