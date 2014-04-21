/*
 * Trigger function to enforce that time based partitioning must use run_maintenance()
 */
CREATE FUNCTION time_partition_maintenance_true_trig() RETURNS trigger 
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
BEGIN

    IF TG_OP = 'INSERT' OR TG_OP = 'UPDATE' THEN
        IF NEW.type IN ('time-static', 'time-dynamic', 'time-custom') AND NEW.use_run_maintenance = FALSE THEN
            RAISE EXCEPTION 'use_run_maintenance cannot be set to FALSE for time based partitioning';
        END IF;
    END IF;
    RETURN NEW;
END
$$;

CREATE TRIGGER time_partition_maintenance_true_trig
BEFORE INSERT OR UPDATE OF type, use_run_maintenance
ON @extschema@.part_config
FOR EACH ROW EXECUTE PROCEDURE @extschema@.time_partition_maintenance_true_trig();


