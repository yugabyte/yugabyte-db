import { Controller, FieldValues, Path, useFormContext } from 'react-hook-form';
import { Trans, TFunction } from 'react-i18next';
import { YBSelect, YBTooltip, mui } from '@yugabyte-ui-library/core';
import { FaultToleranceType } from '../../steps/resilence-regions/dtos';

const { MenuItem, styled } = mui;

interface FaultToleranceTypeFieldProps<T extends FieldValues> {
  name: Path<T>;
  label: string;
  t: TFunction;
  sx?: React.CSSProperties;
  isK8s?: boolean;
  /** Grey-disable Node and None (multi-provider). */
  disableNodeAndNone?: boolean;
}

const MULTI_PROVIDER_DISABLED_FT = new Set<string>([
  FaultToleranceType.NODE_LEVEL,
  FaultToleranceType.NONE
]);

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
  width: '100%',
  boxSizing: 'border-box',
  position: 'relative',
  borderBottom: `1px solid ${theme.palette.grey[300]}`,
  '& > .subText': {
    fontWeight: 400,
    fontSize: theme.typography.subtitle1.fontSize,
    color: theme.palette.grey[600]
  },
  '&.Mui-disabled': {
    opacity: 1,
    color: theme.palette.grey[500],
    '& > .subText': {
      color: theme.palette.grey[500]
    }
  }
}));

export const FaultToleranceTypeField = <T extends FieldValues>({
  name,
  t,
  label,
  sx = {},
  isK8s = false,
  disableNodeAndNone = false
}: FaultToleranceTypeFieldProps<T>) => {
  const { control } = useFormContext<T>();
  const disabledTooltip = (
    <Trans
      t={t}
      i18nKey="multiProviderFtDisabledTooltip"
      components={{ bold: <span style={{ fontWeight: 600 }} /> }}
    />
  );
  return (
    <Controller
      name={name}
      control={control}
      render={({ field }) => {
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
              PaperProps: {
                sx: { width: 'auto' }
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
              const disabled =
                disableNodeAndNone && MULTI_PROVIDER_DISABLED_FT.has(value);
              return (
                <StyledMenuItem key={value} value={value} disabled={disabled}>
                  <span>{t(faultToleranceTitleKey(value, isK8s))}</span>
                  <span className="subText">{t(faultToleranceSubTextKey(value, isK8s))}</span>
                  {disabled && (
                    <YBTooltip title={disabledTooltip} placement="right">
                      <span style={{ position: 'absolute', inset: 0, pointerEvents: 'auto' }} />
                    </YBTooltip>
                  )}
                </StyledMenuItem>
              );
            })}
          </YBSelect>
        );
      }}
    />
  );
};
