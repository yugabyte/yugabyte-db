/* Add jwt_auth_token Column user table */
ALTER TABLE IF EXISTS users ADD COLUMN IF NOT EXISTS oidc_jwt_auth_token TEXT;
ALTER TABLE users ALTER COLUMN oidc_jwt_auth_token TYPE bytea USING pgp_sym_encrypt(oidc_jwt_auth_token::text, 'users::oidc_jwt_auth_token');