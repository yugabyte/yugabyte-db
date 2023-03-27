import React, { Component, FC, useMemo } from 'react';
import { useTranslation } from 'react-i18next';
import { Link, makeStyles } from '@material-ui/core';
import { Layer, Rectangle, ResponsiveContainer, Sankey } from 'recharts';
import { ClusterData, useGetClusterNodesQuery } from '@app/api/src';
import { Link as RouterLink } from 'react-router-dom';
import { AXIOS_INSTANCE } from '@app/api/src';

const useStyles = makeStyles((theme) => ({
  container: {
    height: '186px',
    overflow: 'auto',
    padding: `${theme.spacing(1)}px 0`,
    position: 'relative'
  },
  link: {
    '&:link, &:focus, &:active, &:visited, &:hover': {
      textDecoration: 'none',
      color: theme.palette.text.primary,
    }
  },
  availableContainer: {
    position: 'absolute',
    bottom: 12,
    fontSize: "13px",
    fontWeight: 500,
    display: 'flex',
    gap: theme.spacing(1.5),
  },
  availableText: {
    color: theme.palette.grey[500],
  },
  availableValue: {
    color: '#444',
  }
}));

interface VCpuUsageChartProps {
  cluster: ClusterData,
}

/* const data = {
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
}; */

export const VCpuUsageChartH: FC<VCpuUsageChartProps> = ({ cluster }) => {
  const classes = useStyles();
  const { t } = useTranslation();
  const { data: nodesResponse } = useGetClusterNodesQuery();

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
        { "name": "cores", "translateKey": "clusterDetail.overview" },
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
    <div className={classes.container}>
      <Link className={classes.link} component={RouterLink} to="/performance/metrics">
        <ResponsiveContainer width="99%" height="100%" debounce={2} minWidth={380}>
          <Sankey
            data={data}
            cursor="pointer"
            margin={{
              top: 6,
              left: 142,
              right: 180,
              bottom: 6,
            }}
            node={<CpuSankeyNode translate={t} totalCores={totalCores} 
              totalCpu={data["links"].reduce((acc, curr) => acc + curr.value, 0)} />}
            nodeWidth={4}
            nodePadding={10}
            link={<CpuSankeyLink />}
          >
            {/* <Tooltip /> */}
          </Sankey>
        </ResponsiveContainer>
        <div className={classes.availableContainer}>
          <div className={classes.availableText}>{t((data.nodes[0] as any).translateKey + ".available").toUpperCase()}</div>
          <div className={classes.availableValue}>{totalCores} {data.nodes[0].name}</div>
        </div>
      </Link>
    </div>
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
        <text
          textAnchor='end'
          x={x - 10}
          y={y + height / 2 + width / 2 + 3}
          fontSize="13"
          fontWeight={500}
        >
          
          <tspan fill="#97A5B0">{t(payload.translateKey + ".usage").toUpperCase()}</tspan>
          <tspan dx={cpuUsage < 10 ? 42 : 32} fill="#000" fontWeight={700} fontSize="15">{cpuUsage} </tspan>
          <tspan fill="#444" fillOpacity={1}>{payload.name}</tspan>
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