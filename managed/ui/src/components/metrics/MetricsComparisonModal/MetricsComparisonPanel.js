import {
  CartesianGrid,
  Legend,
  Line,
  LineChart,
  ResponsiveContainer,
  Tooltip,
  XAxis,
  YAxis
} from 'recharts';
import moment from 'moment';
import { METRIC_COLORS } from '../MetricsConfig';
import { useSelector } from 'react-redux';
import { ybFormatDate, YBTimeFormats } from '../../../redesign/helpers/DateUtils';

export const MetricsComparisonPanel = ({ metricsData, metricsKey, metricsLayout, side }) => {
  const currentUser = useSelector((state) => state.customer.currentUser);

  const timeFormatter = (cell, format) => {
    if (currentUser.data.timezone) {
      return moment(cell).tz(currentUser.data.timezone).format(format);
    } else {
      return moment(cell).format(format);
    }
  };

  const yaxisTickFormatter = (value) => {
    let yaxisFormat = Number(value).toLocaleString();
    if (metricsLayout?.yaxis?.ticksuffix) {
      const tickSuffix = metricsLayout.yaxis.ticksuffix.replace('&nbsp;', '\u00a0');
      yaxisFormat += tickSuffix;
    }
    return yaxisFormat;
  };

  // eslint-disable-next-line react/display-name
  const chartLines = metricsData?.names?.map((val, i) => {
    return (
      <Line
        type="monotone"
        key={val}
        dataKey={val}
        dot={false}
        strokeWidth={1.5}
        stroke={METRIC_COLORS[i % METRIC_COLORS.length]}
      />
    );
  });

  return (
    <div className={'metrics-comparison-panel-container ' + side}>
      <div className="metrics-comparison-panel">
        <ResponsiveContainer width="100%" height="100%" debounce={100}>
          <LineChart data={metricsData?.data} syncId={metricsKey} margin={{ left: 15 }}>
            <CartesianGrid vertical={false} strokeDasharray="2 2" />
            <XAxis
              dataKey="x"
              minTickGap={24}
              tickMargin={8}
              fontSize={11.5}
              tickFormatter={(value) => timeFormatter(value, 'MM/DD H:mm')}
            />
            <YAxis
              allowDecimals={false}
              allowDataOverflow
              axisLine={false}
              tickLine={false}
              tickFormatter={yaxisTickFormatter}
              fontSize={11.5}
              width={42}
            />
            <Tooltip
              labelFormatter={(value) =>
                ybFormatDate(value, YBTimeFormats.YB_HOURS_FIRST_TIMESTAMP)
              }
            />
            <Legend align="left" />
            {chartLines}
          </LineChart>
        </ResponsiveContainer>
      </div>
    </div>
  );
};
