-- Copyright (c) YugaByte, Inc.
UPDATE customer_task as ta SET target_uuid = (b.backup_info->>'universeUUID')::uuid 
FROM backup as b WHERE b.customer_uuid=ta.customer_uuid AND ta.target_uuid=b.backup_uuid 
    AND ta.target_type='Backup' AND ta.type='Delete';
