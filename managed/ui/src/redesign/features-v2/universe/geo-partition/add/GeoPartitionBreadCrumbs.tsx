interface GeoPartitionBreadCrumbsProps {
  groupTitle: React.ReactElement;
  subTitle: React.ReactElement;
}
export const GeoPartitionBreadCrumb = ({ groupTitle, subTitle }: GeoPartitionBreadCrumbsProps) => {
  return (
    <div
      style={{
        display: 'flex',
        alignItems: 'center',
        fontSize: '18px',
        fontWeight: '600',
        gap: '12px'
      }}
    >
      <span style={{ color: '#97A5B0' }}>{groupTitle}</span>
      <span style={{ color: '#97A5B0' }}>/</span>
      <span>{subTitle}</span>
    </div>
  );
};

export default GeoPartitionBreadCrumb;
