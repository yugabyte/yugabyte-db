import React, { Component, FC } from 'react';
import { useTranslation } from 'react-i18next';
import { Link, makeStyles } from '@material-ui/core';
import { Layer, Rectangle, ResponsiveContainer, Sankey } from 'recharts';
import type { ClusterData } from '@app/api/src';
import { Link as RouterLink } from 'react-router-dom';

const useStyles = makeStyles((theme) => ({
  container: {
    height: '186px',
    overflow: 'auto',
    padding: `${theme.spacing(1)}px 0`,
  },
  link: {
    '&:link, &:focus, &:active, &:visited, &:hover': {
      textDecoration: 'none',
      color: theme.palette.text.primary,
    }
  }
}));

interface VCpuUsageChartProps {
  cluster: ClusterData,
}

const data = {
  "nodes": [
    { "name": "cores", "translateKey": "clusterDetail.overview" },
    { "name": "cluster_vpn...1-n5" },
    { "name": "cluster_vpn...1-n6" },
    { "name": "cluster_vpn...1-n7" },
    { "name": "cluster_vpn...1-n1" },
    { "name": "cluster_vpn...1-n3" },
    { "name": "cluster_vpn...1-n4" },
    { "name": "cluster_vpn...1-n2" },
    { "name": "cluster_vpn...1-n9" },
    { "name": "cluster_vpn...1-n8" },
  ],
  "links": [
    { "source": 0, "target": 1, "value": 25 },
    { "source": 0, "target": 2, "value": 12 },
    { "source": 0, "target": 3, "value":  7 },
    { "source": 0, "target": 4, "value":  6 },
    { "source": 0, "target": 5, "value":  5 },
    { "source": 0, "target": 6, "value":  5 },
    { "source": 0, "target": 7, "value":  4 },
    { "source": 0, "target": 8, "value":  3 },
    { "source": 0, "target": 9, "value":  2 },
  ]
};

export const VCpuUsageChart: FC<VCpuUsageChartProps> = ({ cluster }) => {
  const classes = useStyles();
  const { t } = useTranslation();

  const totalCores = cluster.spec?.cluster_info?.node_info.num_cores ?? 0;

  return (
    <div className={classes.container}>
      <Link className={classes.link} component={RouterLink} to="/performance/metrics">
        <ResponsiveContainer width="99%" height="100%" debounce={2} minWidth={380}>
          <Sankey
            data={data}
            cursor="pointer"
            margin={{
              top: 6,
              left: 145,
              right: 180,
              bottom: 6,
            }}
            node={<CpuSankeyNode translate={t} 
              nodeCount={data["nodes"].length - 1}
              totalCores={totalCores} /> 
            }
            nodeWidth={4}
            nodePadding={10}
            link={<CpuSankeyLink />}
          >
            {/* <Tooltip /> */}
          </Sankey>
        </ResponsiveContainer>
      </Link>
    </div>
  );
};


function CpuSankeyNode(props: any) {
  const { x, y, width, height, index, payload, translate: t, nodeCount, totalCores } = props;
  const isOut = index === 0 /* x + width + 6 > containerWidth */;
  return (
    <Layer key={`CustomNode${index}`}>
      <Rectangle 
        x={x} y={y} 
        width={width} height={height} 
        fill={isOut ? "#2B59C3" : "#8047F5"} 
        fillOpacity={isOut ? 0.6 : 0.5} />
      {!isOut ? 
        <text
          textAnchor={'start'}
          x={x + width + 15}
          y={y + height / 2 + width / 2 + 3}
          fontSize="12"
          stroke="#888"
          strokeOpacity="0.5"
        >
          <tspan dx={payload.value < 10 ? 6 : 0}>{payload.value}%</tspan>
          <tspan dx={16}>{payload.name}</tspan>
        </text>
        :
        <>
          <text
            textAnchor='end'
            x={x - 10}
            y={y + height / 2 + width / 2 + 3}
            fontSize="13"
            fontWeight={500}
          >
            
            <tspan fill="#97A5B0">{t(payload.translateKey + ".usage").toUpperCase()}</tspan>
            <tspan dx={42} fill="#000" fontWeight={700} fontSize="15">{nodeCount} </tspan>
            <tspan fill="#444" fillOpacity={1}>{payload.name}</tspan>
          </text>
          <text
            textAnchor='end'
            x={x - 10}
            y={y + height / 2 + width / 2 + 100}
            fontSize="13"
            fontWeight={500}
          >
            
            <tspan fill="#97A5B0">{t(payload.translateKey + ".available").toUpperCase()}</tspan>
            <tspan dx={14} fill="#444" fillOpacity={1}>{totalCores} {payload.name}</tspan>
          </text>
        </>
      }
    </Layer>
  );
}

class CpuSankeyLink extends Component<any, any> {
  static displayName = 'CpuSankeyLink';

  render() {
    const { sourceX, targetX, sourceY, targetY, sourceControlX, targetControlX, linkWidth, index } = this.props;

    const gradientID = `linkGradient${index}`;
    const fill = this.state?.fill ?? `url(#${gradientID})`;

    return (
      <Layer key={`CustomLink${index}`}>
        <defs>
          <linearGradient id={gradientID}>
            <stop offset="20%" stopColor={"#2B59C3"} stopOpacity={"0.18"} />
            <stop offset="80%" stopColor={"#8047F5"} stopOpacity={"0.18"} />
          </linearGradient>
        </defs>
        <path
          d={`
            M${sourceX},${sourceY + linkWidth / 2}
            C${sourceControlX},${sourceY + linkWidth / 2}
              ${targetControlX},${targetY + linkWidth / 2}
              ${targetX},${targetY + linkWidth / 2}
            L${targetX},${targetY - linkWidth / 2}
            C${targetControlX},${targetY - linkWidth / 2}
              ${sourceControlX},${sourceY - linkWidth / 2}
              ${sourceX},${sourceY - linkWidth / 2}
            Z
          `}
          fill={fill}
          onMouseEnter={() => {
            this.setState({ fill: 'rgba(0, 136, 254, 0.5)' });
          }}
          onMouseLeave={() => {
            this.setState({ fill: `url(#${gradientID})` });
          }}
        />
      </Layer>
    );
  }
}