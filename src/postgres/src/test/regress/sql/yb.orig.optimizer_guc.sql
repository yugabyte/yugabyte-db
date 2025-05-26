reset yb_enable_cbo;
reset yb_enable_base_scans_cost_model;
reset yb_enable_optimizer_statistics;

show yb_enable_cbo;
show yb_enable_base_scans_cost_model;
show yb_enable_optimizer_statistics;

-- change yb_enable_cbo

set yb_enable_cbo = on;
show yb_enable_cbo;
show yb_enable_base_scans_cost_model;
show yb_enable_optimizer_statistics;

set yb_enable_cbo = off;
show yb_enable_cbo;
show yb_enable_base_scans_cost_model;
show yb_enable_optimizer_statistics;

set yb_enable_cbo = legacy_mode;
show yb_enable_cbo;
show yb_enable_base_scans_cost_model;
show yb_enable_optimizer_statistics;

set yb_enable_cbo = legacy_stats_mode;
show yb_enable_cbo;
show yb_enable_base_scans_cost_model;
show yb_enable_optimizer_statistics;

set yb_enable_cbo = off;
show yb_enable_cbo;
show yb_enable_base_scans_cost_model;
show yb_enable_optimizer_statistics;


-- turn on/off old parameters
set yb_enable_base_scans_cost_model = off;
show yb_enable_cbo;
show yb_enable_base_scans_cost_model;
show yb_enable_optimizer_statistics;
set yb_enable_optimizer_statistics = off;
show yb_enable_cbo;
show yb_enable_base_scans_cost_model;
show yb_enable_optimizer_statistics;
set yb_enable_optimizer_statistics = on;
show yb_enable_cbo;
show yb_enable_base_scans_cost_model;
show yb_enable_optimizer_statistics;
set yb_enable_base_scans_cost_model = on;
show yb_enable_cbo;
show yb_enable_base_scans_cost_model;
show yb_enable_optimizer_statistics;
set yb_enable_base_scans_cost_model = off;
show yb_enable_cbo;
show yb_enable_base_scans_cost_model;
show yb_enable_optimizer_statistics;
set yb_enable_optimizer_statistics = off;
show yb_enable_cbo;
show yb_enable_base_scans_cost_model;
show yb_enable_optimizer_statistics;


-- boolean aliases

set yb_enable_cbo = true;
show yb_enable_cbo;
set yb_enable_cbo = false;
show yb_enable_cbo;
set yb_enable_cbo = yes;
show yb_enable_cbo;
set yb_enable_cbo = no;
show yb_enable_cbo;
set yb_enable_cbo = 1;
show yb_enable_cbo;
set yb_enable_cbo = 0;
show yb_enable_cbo;


-- error
set yb_enable_cbo = oui;

