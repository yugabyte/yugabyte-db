import { Controller, FieldValues, Path, useFormContext } from 'react-hook-form';
import { TFunction } from 'i18next';
import { YBSelect, mui } from '@yugabyte-ui-library/core';
import { FaultToleranceType } from '../../steps/resilence-regions/dtos';

const { MenuItem, styled } = mui;

interface FaultToleranceTypeFieldProps<T extends FieldValues> {
  name: Path<T>;
  label: string;
  t: TFunction;
  sx?: React.CSSProperties;
  isK8s?: boolean;
}

const faultToleranceTitleKey = (value: string, isK8s: boolean) =>
  isK8s && value === FaultToleranceType.NODE_LEVEL ? `${value}.titlePod` : `${value}.title`;

const faultToleranceSubTextKey = (value: string, isK8s: boolean) =>
  isK8s && value === FaultToleranceType.NODE_LEVEL ? `${value}.subTextPod` : `${value}.subText`;


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
  label,
  sx = {},
  isK8s = false
}: FaultToleranceTypeFieldProps<T>) => {
  const { control } = useFormContext<T>();
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
              width: '320px',
              ...sx
            }}
            menuProps={{
              anchorOrigin: {
                vertical: 'bottom',
                horizontal: 'left'
              },
              transformOrigin: {
                vertical: 'top',
                horizontal: 'left'
              },
              MenuListProps: {
                sx: { paddingTop: 0, paddingBottom: 0 }
              }
            }}
            renderValue={(value) => {
              return t(faultToleranceTitleKey(String(value), isK8s));
            }}
            label={label}
            dataTestId="fault-tolerance-type-field"
          >
            {Object.keys(FaultToleranceType).map((key) => {
              const value = FaultToleranceType[key as keyof typeof FaultToleranceType];
              return (
                <StyledMenuItem key={value} value={value}>
                  <span>{t(faultToleranceTitleKey(value, isK8s))}</span>
                  <span className="subText">{t(faultToleranceSubTextKey(value, isK8s))}</span>
                </StyledMenuItem>
              );
            })}
          </YBSelect>
        );
      }}
    />
  );
};
