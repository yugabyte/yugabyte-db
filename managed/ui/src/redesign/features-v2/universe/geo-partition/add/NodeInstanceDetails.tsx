import { mui } from '@yugabyte-ui-library/core';
import { FC, useState } from 'react';
import { useTranslation } from 'react-i18next';
import NodesIcon from '@app/redesign/assets/nodes.svg';
import { ArrowDropDown } from '@material-ui/icons';
import { ClusterSpec } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

const { Box, styled, Fade } = mui;


const NodesAccordion = styled('div')(({ theme, expanded }) => ({
    border: `1px solid ${theme.palette.grey[200]}`,
    borderRadius: '8px',
    background: '#FBFCFD',
    height: expanded ? 'auto' : '56px',
    width: '100%',
    cursor: 'pointer',
    padding: '16px',
    '& .header': {
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'space-between',
        fontSize: '13px',
        fontWeight: 600,
        color: theme.palette.grey[900],
        '& svg': {
            width: '24px',
            height: '24px'
        }
    },
    '& .details': {
        marginTop: '24px',
        columnGap: '32px',
        '& tr > * + *': {
            paddingLeft: '32px',
            paddingBottom: '6px'
        },
        '& .attrib': {
            fontSize: '11.5px',
            fontWeight: 500,
            color: theme.palette.grey[500],
            textTransform: 'uppercase'
        },
        '& .value': {
            fontSize: '13px',
            fontWeight: 400,
            color: theme.palette.grey[700]
        }
    }
}));

interface NodeInstanceDetailsProps {
    cluster: ClusterSpec;
}

export const NodeInstanceDetails: FC<NodeInstanceDetailsProps> = ({ cluster }) => {
    const [isNodesAccordionOpen, setIsNodesAccordionOpen] = useState(false);
    const { t: tGeoPartition } = useTranslation('translation', {
        keyPrefix: 'geoPartition.geoPartitionNodesAndAvailability'
    });

    const nodeSpec = cluster?.node_spec;
    const instanceType = nodeSpec?.instance_type ?? '-';
    const volumeSize = nodeSpec?.storage_spec?.volume_size ?? '-';
    const volumeType = nodeSpec?.storage_spec?.storage_type ?? '-';
    const iops = nodeSpec?.storage_spec?.disk_iops ?? '-';
    const throughput = nodeSpec?.storage_spec?.throughput ?? '-';

    return (
        <NodesAccordion
            onClick={() => setIsNodesAccordionOpen(!isNodesAccordionOpen)}
            expanded={isNodesAccordionOpen}
        >
            <div className="header">
                <Box sx={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
                    <NodesIcon />
                    {tGeoPartition('nodes')}
                </Box>
                <ArrowDropDown
                    style={{
                        transform: isNodesAccordionOpen ? 'rotate(180deg)' : 'rotate(0deg)',
                        transition: 'transform 0.3s ease'
                    }}
                />
            </div>
            <Fade in={isNodesAccordionOpen} timeout={300}>
                <Box className="details">
                    <table>
                        <tbody>
                            <tr>
                                <td className="attrib">{tGeoPartition('instanceType')}</td>
                                <td>{instanceType}</td>
                            </tr>
                            <tr>
                                <td className="attrib">{tGeoPartition('volume')}</td>
                                <td>{volumeSize}</td>
                            </tr>
                            <tr>
                                <td className="attrib">{tGeoPartition('ebsType')}</td>
                                <td>{volumeType}</td>
                            </tr>
                            <tr>
                                <td className="attrib">{tGeoPartition('iops')}</td>
                                <td>{iops}</td>
                            </tr>
                            <tr>
                                <td className="attrib">{tGeoPartition('throughput')}</td>
                                <td>{throughput}</td>
                            </tr>
                        </tbody>
                    </table>
                    <Box></Box>
                </Box>
            </Fade>
        </NodesAccordion>
    );
};
