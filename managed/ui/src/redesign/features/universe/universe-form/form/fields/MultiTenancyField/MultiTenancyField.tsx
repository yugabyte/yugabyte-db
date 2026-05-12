import { FC } from 'react';
import { useUpdateEffect } from 'react-use';
import { Box, makeStyles, Typography } from '@material-ui/core';
import { Controller, useFormContext, useWatch } from 'react-hook-form';
import {
  YBToggleField,
  YBLabel,
  YBTooltip,
  YBEarlyAccessTag,
  YBInput
} from '../../../../../../components';
import { UniverseFormData } from '../../../utils/dto';
import {
  MULTI_TENANCY_QOS_FIELD,
  MULTI_TENANCY_QOS_MAX_DB_CPU_FIELD,
  MULTI_TENANCY_QOS_MAX_DB_COUNT_FIELD,
  YSQL_FIELD,
  YCQL_FIELD
} from '../../../utils/constants';
import {
  clampMultiTenancyQosMaxDbCpuPercent,
  clampMultiTenancyQosMaxDbCount,
  MULTI_TENANCY_QOS_MAX_DB_CPU_PERCENT_FALLBACK,
  MULTI_TENANCY_QOS_MAX_DB_CPU_PERCENT_MAX,
  MULTI_TENANCY_QOS_MAX_DB_CPU_PERCENT_MIN,
  MULTI_TENANCY_QOS_MAX_DB_CPU_PERCENT_STEP,
  MULTI_TENANCY_QOS_MAX_DB_COUNT_FALLBACK,
  MULTI_TENANCY_QOS_MAX_DB_COUNT_MIN,
  MULTI_TENANCY_QOS_MAX_DB_COUNT_STEP
} from '../../../../universe-actions/edit-multi-tenancy/multiTenancyQosDbCount';

interface MultiTenancyFieldProps {
  disabled: boolean;
}

const useStyles = makeStyles((theme) => ({
  subText: {
    fontSize: '11.5px',
    lineHeight: '16px',
    fontWeight: 400,
    color: '#67666C'
  },
  qosNestedFields: {
    display: 'flex',
    flexDirection: 'column',
    marginTop: theme.spacing(2),
    maxWidth: 280,
    rowGap: theme.spacing(2)
  }
}));

export const MultiTenancyField: FC<MultiTenancyFieldProps> = ({ disabled }) => {
  const { control, setValue } = useFormContext<UniverseFormData>();
  const classes = useStyles();

  const isYSQLEnabled = useWatch({ name: YSQL_FIELD });
  const isYCQLEnabled = useWatch({ name: YCQL_FIELD });
  const isMultiTenancyEnabled = useWatch({ name: MULTI_TENANCY_QOS_FIELD });

  useUpdateEffect(() => {
    if (!isYSQLEnabled || isYCQLEnabled) setValue(MULTI_TENANCY_QOS_FIELD, false);
  }, [isYSQLEnabled, isYCQLEnabled]);

  const toggleDisabled = disabled || !isYSQLEnabled || isYCQLEnabled;

  return (
    <Box
      display="flex"
      alignItems="flex-start"
      width="100%"
      data-testid="MultiTenancyField-Container"
    >
      <YBTooltip
        title={
          toggleDisabled ? (
            <Typography className={classes.subText}>
              {!isYSQLEnabled
                ? 'Enable YSQL first.'
                : 'Turn off YCQL for multi-tenancy (internal test toggle).'}
            </Typography>
          ) : (
            ''
          )
        }
      >
        <div>
          <YBToggleField
            name={MULTI_TENANCY_QOS_FIELD}
            inputProps={{
              'data-testid': 'MultiTenancyField-Toggle'
            }}
            control={control}
            disabled={toggleDisabled}
          />
        </div>
      </YBTooltip>
      <Box display="flex" flexDirection="column" flex={1} minWidth="min-content">
        <YBLabel dataTestId="MultiTenancyField-Label" width="420px">
          Enable multi-tenancy (QoS)&nbsp;
          <YBEarlyAccessTag />
        </YBLabel>
        <Typography className={classes.subText}>
          Internal test only: enable QoS multi-tenancy on create.
          <br />
          Requires compatible DB version, runtime flags, and cgroup setup per platform docs.
        </Typography>
        {isMultiTenancyEnabled && !toggleDisabled && (
          <Box className={classes.qosNestedFields}>
            <Controller
              name={MULTI_TENANCY_QOS_MAX_DB_CPU_FIELD}
              control={control}
              render={({ field }) => (
                <YBInput
                  type="number"
                  fullWidth
                  trimWhitespace={false}
                  disabled={disabled}
                  label="Max DB CPU %"
                  value={
                    field.value === undefined || field.value === null ? '' : String(field.value)
                  }
                  helperText="0–100"
                  inputProps={{
                    min: MULTI_TENANCY_QOS_MAX_DB_CPU_PERCENT_MIN,
                    max: MULTI_TENANCY_QOS_MAX_DB_CPU_PERCENT_MAX,
                    step: MULTI_TENANCY_QOS_MAX_DB_CPU_PERCENT_STEP,
                    'data-testid': 'MultiTenancyField-QosMaxDbCpuPercent'
                  }}
                  onChange={(event) => {
                    const raw = event.target.value;
                    if (raw === '') {
                      field.onChange('');
                      return;
                    }
                    const parsed = parseFloat(raw);
                    field.onChange(Number.isFinite(parsed) ? parsed : '');
                  }}
                  onBlur={(event) => {
                    field.onBlur();
                    const clamped = clampMultiTenancyQosMaxDbCpuPercent(
                      event.target.value === ''
                        ? MULTI_TENANCY_QOS_MAX_DB_CPU_PERCENT_FALLBACK
                        : event.target.value
                    );
                    field.onChange(clamped);
                  }}
                />
              )}
            />
            <Controller
              name={MULTI_TENANCY_QOS_MAX_DB_COUNT_FIELD}
              control={control}
              render={({ field }) => (
                <YBInput
                  type="number"
                  fullWidth
                  trimWhitespace={false}
                  disabled={disabled}
                  label="Max DB count"
                  value={
                    field.value === undefined || field.value === null ? '' : String(field.value)
                  }
                  helperText="Integer ≥ 1"
                  inputProps={{
                    min: MULTI_TENANCY_QOS_MAX_DB_COUNT_MIN,
                    step: MULTI_TENANCY_QOS_MAX_DB_COUNT_STEP,
                    'data-testid': 'MultiTenancyField-QosMaxDbCount'
                  }}
                  onChange={(event) => {
                    const raw = event.target.value;
                    if (raw === '') {
                      field.onChange('');
                      return;
                    }
                    const parsed = parseInt(raw, 10);
                    field.onChange(Number.isFinite(parsed) ? parsed : '');
                  }}
                  onBlur={(event) => {
                    field.onBlur();
                    const clamped = clampMultiTenancyQosMaxDbCount(
                      event.target.value === ''
                        ? MULTI_TENANCY_QOS_MAX_DB_COUNT_FALLBACK
                        : event.target.value
                    );
                    field.onChange(clamped);
                  }}
                />
              )}
            />
          </Box>
        )}
      </Box>
    </Box>
  );
};
