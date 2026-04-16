ALTER TABLE IF EXISTS users ADD COLUMN IF NOT EXISTS oidc_jwt_auth_token TEXT;
ALTER TABLE users ALTER COLUMN oidc_jwt_auth_token TYPE binary varying;