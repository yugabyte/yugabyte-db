/* pg_hint_plan/pg_hint_plan--1.0.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION pg_hint_plan" to load this file. \quit

CREATE TABLE hint_plan.hints (
	norm_query_string	text	NOT NULL,
	application_name	text	NOT NULL,
	hints				text	NOT NULL,
	PRIMARY KEY (norm_query_string, application_name)
);
