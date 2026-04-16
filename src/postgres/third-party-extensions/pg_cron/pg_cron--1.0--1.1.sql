/* pg_cron--1.0--1.1.sql */

ALTER TABLE cron.job ADD COLUMN active boolean not null default 'true';
