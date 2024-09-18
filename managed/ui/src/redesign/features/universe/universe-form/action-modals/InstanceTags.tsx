import { Box } from '@material-ui/core';

interface InstanceTagsProps {
  tags: Record<string, string>;
}

export const InstanceTags = ({ tags }: InstanceTagsProps) => {
  return (
    <>
      {Object.entries(tags ?? {}).map(([key, value]) => (
        <Box key={key}>
          <span>{`${key}: ${value}`}</span>
        </Box>
      ))}
    </>
  );
};