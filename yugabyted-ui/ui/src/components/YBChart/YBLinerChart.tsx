import React, { FC, useState } from 'react';
import { Area, CartesianGrid, Legend, Line, ResponsiveContainer, Tooltip, XAxis, YAxis, ComposedChart } from 'recharts';
import { CHART_RESIZE_DEBOUNCE } from '@app/helpers';
import { colors, themeVariables as variables } from '@app/theme/variables';
import type { CurveType } from 'recharts/types/shape/Curve';
import { YBChartLegend, LegendItem } from '@app/components/YBChart/YBChartLegend';

export interface YBLineChartLegend {
  visible?: boolean;
  isSelectable?: boolean;
  icon?: 'circle' | 'square';
}

export interface YBLineChartCartesianGrid {
  stroke?: string;
  strokeDasharray?: string;
  isVisible?: boolean;
  vertical?: boolean;
}

export interface YBLineChartLine {
  StrokeWidth?: number;
  type?: CurveType;
  dot?: boolean;
}

export interface YBToolTip {
  isVisible?: boolean;
  separator?: string;
  formatter?: (value: number, name: string) => string[];
  labelFormatter?: (value: string) => string;
}

export interface YBLineChartOptions {
  lineLabels: string[];
  dataKeys: string[];
  strokes?: string[];
  fills?: string[];
  xAxisDataKey: string;
  axisStrokeColor?: string;
  axisFontSize?: number;
  xAxisTickFormatter?: (value: string) => string;
  yAxisTickFormatter?: (value: string) => string;
  legend?: YBLineChartLegend;
  cartesianGrid?: YBLineChartCartesianGrid;
  line?: YBLineChartLine;
  toolTip?: YBToolTip;
  chartDrawingType: ('line' | 'area' | 'bar')[];
}

interface YBLineChartProps {
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  data: any[];
  options: YBLineChartOptions;
}

export const YBLinerChart: FC<YBLineChartProps> = ({ data, options }) => {
  const strokes = options?.strokes ?? Object.values(colors.chartStroke);
  const fills = options?.fills ?? Object.values(colors.chartFill);
  const [selected, setSelected] = useState<number | null>(null);

  return (
    <ResponsiveContainer width="100%" height="100%" debounce={CHART_RESIZE_DEBOUNCE}>
      <ComposedChart data={data}>
        <CartesianGrid
          vertical={options?.cartesianGrid?.vertical ?? false}
          stroke={options?.cartesianGrid?.stroke ?? colors.grey[200]}
          strokeDasharray={options?.cartesianGrid?.strokeDasharray ?? '2 2'}
        />
        <XAxis
          dataKey={options.xAxisDataKey}
          minTickGap={24}
          tickMargin={8}
          tickFormatter={options.xAxisTickFormatter}
          stroke={options?.axisStrokeColor ?? colors.grey[600]}
          fontSize={options?.axisFontSize ?? 11.5}
        />
        <YAxis
          allowDecimals={false}
          allowDataOverflow
          axisLine={false}
          tickLine={false}
          tickFormatter={options.yAxisTickFormatter}
          stroke={options?.axisStrokeColor}
          fontSize={options?.axisFontSize ?? 11.5}
          width={42}
        />
        {options.dataKeys.map((dataKey, index) => {
          if (selected !== null && index !== selected) {
            return null;
          }
          switch (options?.chartDrawingType[index]) {
            case 'line':
              return (
                <Line
                  key={dataKey}
                  name={options.lineLabels[index]}
                  type={options?.line?.type ?? 'monotone'}
                  dataKey={dataKey}
                  dot={options.line?.dot ?? false}
                  stroke={strokes[index]}
                  strokeWidth={options?.line?.StrokeWidth}
                />
              );
            case 'area':
              return (
                <Area
                  key={dataKey}
                  name={options.lineLabels[index]}
                  type={options?.line?.type ?? 'monotone'}
                  dataKey={dataKey}
                  dot={options.line?.dot ?? false}
                  stroke={strokes[index]}
                  strokeWidth={options?.line?.StrokeWidth}
                  fill={fills[index]}
                />
              );
            default:
              return (
                <Line
                  key={dataKey}
                  name={options.lineLabels[index]}
                  type={options?.line?.type ?? 'monotone'}
                  dataKey={dataKey}
                  dot={options.line?.dot ?? false}
                  stroke={strokes[index]}
                  strokeWidth={options?.line?.StrokeWidth}
                />
              );
          }
        })}
        {options?.toolTip?.isVisible && (
          <Tooltip
            separator={options?.toolTip?.separator ?? ''}
            contentStyle={{ borderRadius: variables.borderRadius }}
            formatter={options?.toolTip?.formatter}
            labelFormatter={options?.toolTip?.labelFormatter}
          />
        )}
        {options?.legend?.visible && (
          <Legend
            align="left"
            verticalAlign="bottom"
            content={
              <YBChartLegend
                onSelect={setSelected}
                isSelectable={options?.legend?.isSelectable ?? false}
                items={options.lineLabels.map(
                  (lineLabel, index): LegendItem => ({
                    title: lineLabel,
                    icon: options?.legend?.icon ?? 'circle',
                    fillColor: strokes[index]
                  })
                )}
              />
            }
          />
        )}
      </ComposedChart>
    </ResponsiveContainer>
  );
};
