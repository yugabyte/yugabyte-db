import { Controller, FieldValues, Path, useFormContext } from 'react-hook-form';
import { TFunction } from 'i18next';
import { YBSelect, mui } from '@yugabyte-ui-library/core';
import { FaultToleranceType } from '../../steps/resilence-regions/dtos';

const { MenuItem, styled } = mui;

interface FaultToleranceTypeFieldProps<T extends FieldValues> {
  name: Path<T>;
  label: string;
  t: TFunction;
}

const StyledMenuItem = styled(MenuItem)(({ theme }) => ({
  padding: '8px 16px !important',
  display: 'flex',
  flexDirection: 'column',
  alignItems: 'flex-start !important',
  gap: '8px',
  fontSize: theme.typography.body1.fontSize,
  fontWeight: 600,
  lineHeight: '16px',
  height: '56px !important',
  borderBottom: `1px solid ${theme.palette.grey[300]}`,
  '& > .subText': {
    fontWeight: 400,
    fontSize: theme.typography.subtitle1.fontSize,
    color: theme.palette.grey[600]
  }
}));

export const FaultToleranceTypeField = <T extends FieldValues>({
  name,
  t,
  label
}: FaultToleranceTypeFieldProps<T>) => {
  const { control, getValues, setValue } = useFormContext<T>();
  return (
    <Controller
      name={name}
      control={control}
      render={({ field, fieldState }) => {
        return (
          <YBSelect
            value={field.value}
            onChange={(e) => field.onChange(e.target.value)}
            sx={{
              width: '320px'
            }}
            menuProps={{
              anchorOrigin: {
                vertical: 'bottom',
                horizontal: 'left'
              },
              transformOrigin: {
                vertical: 'top',
                horizontal: 'left'
              }
            }}
            renderValue={(value) => {
              return t(`${value}.title`);
            }}
            label={label}
            dataTestId="fault-tolerance-type-field"
          >
            {Object.keys(FaultToleranceType).map((key) => {
              const value = FaultToleranceType[key as keyof typeof FaultToleranceType];
              return (
                <StyledMenuItem key={value} value={value}>
                  <span>{t(`${value}.title`)}</span>
                  <span className="subText">{t(`${value}.subText`)}</span>
                </StyledMenuItem>
              );
            })}
          </YBSelect>
        );
      }}
    />
  );
};
