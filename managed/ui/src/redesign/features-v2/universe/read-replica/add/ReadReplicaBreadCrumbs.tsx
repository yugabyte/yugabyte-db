interface RRBreadCrumbsProps {
  groupTitle: React.ReactElement;
  subTitle: React.ReactElement;
}

export const RRBreadCrumbs = ({ groupTitle, subTitle }: RRBreadCrumbsProps) => {
  return (
    <div
      style={{
        display: 'flex',
        alignItems: 'center',
        fontSize: '18px',
        fontWeight: '600',
        gap: '12px',
        padding: '24px 8px 16px 8px'
      }}
    >
      <span style={{ color: '#97A5B0' }}>{groupTitle}</span>
      <span style={{ color: '#97A5B0' }}>/</span>
      <span>{subTitle}</span>
    </div>
  );
};
