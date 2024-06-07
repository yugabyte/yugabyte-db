SET statement_timeout = 0;
SET lock_timeout = 0;
SET idle_in_transaction_session_timeout = 0;
SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;
SELECT pg_catalog.set_config('search_path', '', false);
SET check_function_bodies = false;
SET client_min_messages = warning;
SET row_security = off;

CREATE FUNCTION public.notice_on_trigger() RETURNS trigger
    LANGUAGE plpgsql
    AS $$
BEGIN RAISE NOTICE 'Trigger called: %', TG_NAME;
RETURN NEW;
END;
$$;

ALTER FUNCTION public.notice_on_trigger() OWNER TO yugabyte;

SET default_tablespace = '';

SET default_with_oids = false;

CREATE TABLE public.with_constraints__fk_ref (
    id integer
);

ALTER TABLE public.with_constraints__fk_ref OWNER TO yugabyte;


CREATE TABLE public.with_constraints_and_such (
    id integer NOT NULL,
    arr integer[],
    fk integer,
    checked_nn integer NOT NULL,
    indexed integer,
    indexed_constr integer,
    CONSTRAINT with_constraints_and_such_checked_nn_check CHECK ((checked_nn > 0))
);

ALTER TABLE public.with_constraints_and_such OWNER TO yugabyte;

COPY public.with_constraints__fk_ref (id) FROM stdin;
10
20
30
\.

COPY public.with_constraints_and_such (id, arr, fk, checked_nn, indexed, indexed_constr) FROM stdin;
1	{1,2}	10	123	321	111
2	{2,3}	20	234	432	222
3	{3,4}	30	345	543	333
\.

ALTER TABLE ONLY public.with_constraints_and_such
    ADD CONSTRAINT with_constraints_idx UNIQUE (indexed_constr);

ALTER TABLE ONLY public.with_constraints_and_such
    ADD CONSTRAINT with_constraints_pk PRIMARY KEY (id);

CREATE UNIQUE INDEX with_constraints__fk_ref_id_idx ON public.with_constraints__fk_ref USING btree (id);

CREATE INDEX with_constraints_and_such_indexed_idx ON public.with_constraints_and_such USING btree (indexed);

CREATE TRIGGER with_constraints__after_delete_row AFTER DELETE ON public.with_constraints_and_such FOR EACH ROW EXECUTE PROCEDURE public.notice_on_trigger();
CREATE TRIGGER with_constraints__after_delete_stmt AFTER DELETE ON public.with_constraints_and_such FOR EACH STATEMENT EXECUTE PROCEDURE public.notice_on_trigger();
CREATE TRIGGER with_constraints__after_insert_row AFTER INSERT ON public.with_constraints_and_such FOR EACH ROW EXECUTE PROCEDURE public.notice_on_trigger();
CREATE TRIGGER with_constraints__after_insert_stmt AFTER INSERT ON public.with_constraints_and_such FOR EACH STATEMENT EXECUTE PROCEDURE public.notice_on_trigger();
CREATE TRIGGER with_constraints__after_update_row AFTER UPDATE ON public.with_constraints_and_such FOR EACH ROW EXECUTE PROCEDURE public.notice_on_trigger();
CREATE TRIGGER with_constraints__after_update_stmt AFTER UPDATE ON public.with_constraints_and_such FOR EACH STATEMENT EXECUTE PROCEDURE public.notice_on_trigger();
CREATE TRIGGER with_constraints__before_delete_row BEFORE DELETE ON public.with_constraints_and_such FOR EACH ROW EXECUTE PROCEDURE public.notice_on_trigger();
CREATE TRIGGER with_constraints__before_delete_stmt BEFORE DELETE ON public.with_constraints_and_such FOR EACH STATEMENT EXECUTE PROCEDURE public.notice_on_trigger();
CREATE TRIGGER with_constraints__before_insert_row BEFORE INSERT ON public.with_constraints_and_such FOR EACH ROW EXECUTE PROCEDURE public.notice_on_trigger();
CREATE TRIGGER with_constraints__before_insert_stmt BEFORE INSERT ON public.with_constraints_and_such FOR EACH STATEMENT EXECUTE PROCEDURE public.notice_on_trigger();
CREATE TRIGGER with_constraints__before_update_c_row BEFORE UPDATE OF id, arr ON public.with_constraints_and_such FOR EACH ROW EXECUTE PROCEDURE public.notice_on_trigger();
CREATE TRIGGER with_constraints__before_update_row BEFORE UPDATE ON public.with_constraints_and_such FOR EACH ROW EXECUTE PROCEDURE public.notice_on_trigger();
CREATE TRIGGER with_constraints__before_update_stmt BEFORE UPDATE ON public.with_constraints_and_such FOR EACH STATEMENT EXECUTE PROCEDURE public.notice_on_trigger();

ALTER TABLE ONLY public.with_constraints_and_such
    ADD CONSTRAINT with_constraints_fk FOREIGN KEY (fk) REFERENCES public.with_constraints__fk_ref(id);
