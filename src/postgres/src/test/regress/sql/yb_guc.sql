-- Check transaction priority bounds.

set log_error_verbosity = default;

-- Values should be in interval [0,1] (inclusive).
-- Invalid values.
set yb_transaction_priority_upper_bound = 2;
set yb_transaction_priority_lower_bound = -1;

-- Valid values.
set yb_transaction_priority_upper_bound = 1;
set yb_transaction_priority_lower_bound = 0;
set yb_transaction_priority_lower_bound = 0.3;
set yb_transaction_priority_upper_bound = 0.7;

-- Lower bound should be less or equal to upper bound.
-- Invalid values.
set yb_transaction_priority_upper_bound = 0.2;
set yb_transaction_priority_lower_bound = 0.8;

-- Valid values.
set yb_transaction_priority_upper_bound = 0.3;
set yb_transaction_priority_upper_bound = 0.6;
set yb_transaction_priority_lower_bound = 0.4;
set yb_transaction_priority_lower_bound = 0.6;
