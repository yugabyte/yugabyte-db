import React, { Component, FC, useMemo } from 'react';
import { useTranslation } from 'react-i18next';
import { Box, makeStyles, Paper, Typography } from '@material-ui/core';
import { Layer, Rectangle, Sankey } from 'recharts';
import { AXIOS_INSTANCE, ClusterData, useGetClusterNodesQuery } from '@app/api/src';

const useStyles = makeStyles((theme) => ({
  container: {
    padding: theme.spacing(2),
    marginBottom: theme.spacing(2.5),
  },
  chartContainer: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',
    height: '150px',
    padding: theme.spacing(1, 1, 2, 1),
    transform: 'rotate(90deg)',
    pointerEvents: 'none'
  },
}));

interface VCpuUsageChartProps {
  cluster: ClusterData,
}

/* const data = {
  "nodes": [
    { "name": "cores", "translateKey": "clusterDetail.performance.metrics" },
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
}; */

export const VCpuUsageChartV: FC<VCpuUsageChartProps> = ({ cluster }) => {
  const classes = useStyles();
  const { t } = useTranslation();
  const { data: nodesResponse } = useGetClusterNodesQuery({});

  const totalCores = cluster.spec?.cluster_info?.node_info.num_cores ?? 0;

  const [nodeCpuUsage, setNodeCpuUsage] = React.useState<number[]>([]);
  React.useEffect(() => {
    if (!nodesResponse) {
      return;
    }

    const populateCpu = async () => {
      const getNodeCpu = async (nodeName: string) => {
        try {
          const cpu = await AXIOS_INSTANCE.get('/metrics?metrics=CPU_USAGE_SYSTEM%2CCPU_USAGE_USER&node_name=' + nodeName)
            .then(({ data }) => (Number(data.data[0].values[1]) || 0) + (Number(data.data[1].values[1]) || 0))
            .catch(err => { console.error(err); return 0; })
          return cpu;
        } catch (err) {
          console.error(err);
          return 0;
        }
      }
      
      const cpuUsage: number[] = [];
      for (let i = 0; i < nodesResponse.data.length; i++) {
        const node = nodesResponse.data[i].name;
        const nodeCPU = await getNodeCpu(node);
        cpuUsage.push(nodeCPU);
      }

      setNodeCpuUsage(cpuUsage);
    }

    populateCpu();
  }, [nodesResponse])

  const data = useMemo(() => {
    return {
      nodes: [
        { "name": "cores", "translateKey": "clusterDetail.performance.metrics" },
        ...(nodesResponse?.data.map(({ name }) => ({ name })) ?? [])
      ],
      links: nodesResponse?.data.map((_, index) => ({ 
        "source": 0,
        "target": index + 1,
        "value": !nodeCpuUsage[index] || nodeCpuUsage[index] < 1 ? 1 : Math.round(nodeCpuUsage[index] * 100) / 100 }
      )) ?? [],
    }
  }, [nodeCpuUsage, nodesResponse])

  return (
    <Paper className={classes.container}>
      <Typography variant="h5">{t('clusterDetail.performance.metrics.vCpuUsage')}</Typography>
      <Box className={classes.chartContainer}>
        <Sankey
          width={150}
          height={900}
          data={data}
          margin={{
            top: 6,
            left: 45,
            right: 35,
            bottom: 6,
          }}
          node={<CpuSankeyNode translate={t} totalCores={totalCores} 
            totalCpu={data["links"].reduce((acc, curr) => acc + curr.value, 0)} /> 
          }
          nodeWidth={4}
          nodePadding={50}
          link={<CpuSankeyLink />}
        >
          {/* <Tooltip /> */}
        </Sankey>
      </Box>
    </Paper>
  );
};


function CpuSankeyNode(props: any) {
  const { x, y, width, height, index, payload, translate: t, totalCores, totalCpu } = props;
  const isOut = index === 0 /* x + width + 6 > containerWidth */;
  const cpuUsage = Math.ceil(totalCpu * totalCores / 100);

  return (
    <Layer key={`CustomNode${index}`}>
      <Rectangle 
        x={x} y={y} 
        width={width} height={height} 
        fill={isOut ? "#2B59C3" : "#8047F5"} 
        fillOpacity={isOut ? 0.6 : 0.5} />
      {!isOut ? 
        <text
          textAnchor='middle'
          x={x + width + 20}
          y={y + height / 2 + width / 2}
          fontSize="12"
          /* 420px y px */
          style={{ transformOrigin: `${x + width + 20}px ${y + height / 2 + width / 2}px`, transform: 'rotate(270deg)' }}
          stroke="#888"
          strokeOpacity="0.5"
        >
          <tspan>{payload.value}%</tspan>
          {/* <tspan dx={16}>{payload.name}</tspan> */}
        </text>
        :
        <text
          textAnchor='middle'
          x={x - 35}
          y={y + height / 2 + width / 2}
          style={{ transformOrigin: `${x - 35}px ${y + height / 2 + width / 2}px`, transform: 'rotate(270deg)' }}
          fontSize="13"
          fontWeight={500}
        >
          
          <tspan fill="#97A5B0">{t(payload.translateKey + ".totalVCpu").toUpperCase()}</tspan>
          <tspan y={y + height / 2 + width / 2} dy={20} x={x - 35} fill="#000" 
            fontWeight={700} fontSize="15">{cpuUsage} {payload.name}
          </tspan>
          <tspan fill="#444" fillOpacity={1}>&nbsp;/ {totalCores} {payload.name}</tspan>
        </text>
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
