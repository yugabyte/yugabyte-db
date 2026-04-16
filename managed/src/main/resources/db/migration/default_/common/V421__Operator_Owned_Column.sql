ALTER TABLE schedule ADD COLUMN IF NOT EXISTS is_kubernetes_operator_controlled boolean DEFAULT false;
ALTER TABLE release ADD COLUMN IF NOT EXISTS is_kubernetes_operator_controlled boolean DEFAULT false;
ALTER TABLE customer_config ADD COLUMN IF NOT EXISTS is_kubernetes_operator_controlled boolean DEFAULT false;
ALTER TABLE backup ADD COLUMN IF NOT EXISTS is_kubernetes_operator_controlled boolean DEFAULT false;