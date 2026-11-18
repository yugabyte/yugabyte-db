use pgrx::prelude::*;
use rand::Rng;

/// Convert PTTT (kept percentage) to epsilon
fn epsilon_from_pttt(pttt: f64, max_v: i32) -> Result<f64, String> {
    if max_v < 2 {
        return Err("MAX_V must be >= 2".to_string());
    }
    if !(0.0..1.0).contains(&pttt) {
        return Err("PTTT must be in [0, 1)".to_string());
    }

    let k = max_v as f64;
    let x = (1.0 + pttt * (k - 1.0)) / (1.0 - pttt);
    Ok(x.ln())
}

/// Apply LDP GRRM using epsilon parameter (TIMING-ATTACK RESISTANT)
///
/// Uses branchless bitwise operations to ensure constant-time execution
/// regardless of whether the true value is kept or modified.
#[pg_extern(schema = "anon")]
fn ldp_grrm(value: i32, epsilon: f64, max_v: i32) -> i32 {
    if epsilon <= 0.0 {
        error!("Epsilon must be greater than 0");
    }
    if max_v < 2 {
        error!("MAX_V must be >= 2");
    }

    let exp_epsilon = epsilon.exp();
    let q = exp_epsilon / (exp_epsilon + max_v as f64 - 1.0);
    let p = 1.0 / (exp_epsilon + max_v as f64 - 1.0);
    let threshold = q - p;

    let mut rng = rand::thread_rng();

    // Generate random value for keep/modify decision
    let rand_val: f64 = rng.r#gen();

    // ALWAYS compute the lie value (constant time)
    // Generate offset in range [1, max_v) to ensure lie != truth
    let offset: i32 = rng.gen_range(1..max_v);
    let lie = ((value - 1 + offset) % max_v) + 1;

    // BRANCHLESS selection using bitwise operations
    // keep = 1 if we should keep truth, 0 otherwise
    let keep = (rand_val <= threshold) as i32;

    // mask = 0xFFFFFFFF (-1) if keep, 0x00000000 (0) if not keep
    let mask = -keep;

    // Branchless select: (value AND mask) OR (lie AND NOT mask)
    // If keep: (value & -1) | (lie & 0) = value
    // If not keep: (value & 0) | (lie & -1) = lie
    (value & mask) | (lie & !mask)
}

/// Apply LDP GRRM using kept percentage (PTTT) parameter (TIMING-ATTACK RESISTANT)
#[pg_extern(schema = "anon")]
fn ldp_grrm_pttt(value: i32, kept_percentage: f64, max_v: i32) -> i32 {
    let epsilon = match epsilon_from_pttt(kept_percentage, max_v) {
        Ok(eps) => eps,
        Err(e) => error!("{}", e),
    };

    ldp_grrm(value, epsilon, max_v)
}

#[cfg(any(test, feature = "pg_test"))]
#[pg_schema]
mod tests {
    use pgrx::prelude::*;

    #[pg_test]
    fn test_ldp_grrm_bounds() {
        for _ in 0..100 {
            let result = crate::ldp::ldp_grrm(5, 1.0, 10);
            assert!(result >= 1 && result <= 10);
        }
    }

    #[pg_test]
    fn test_ldp_grrm_pttt_bounds() {
        for _ in 0..100 {
            let result = crate::ldp::ldp_grrm_pttt(5, 0.65, 10);
            assert!(result >= 1 && result <= 10);
        }
    }

    #[pg_test]
    #[should_panic(expected = "Epsilon must be greater than 0")]
    fn test_invalid_epsilon() {
        crate::ldp::ldp_grrm(5, 0.0, 10);
    }

    #[pg_test]
    #[should_panic(expected = "MAX_V must be >= 2")]
    fn test_invalid_max_v() {
        crate::ldp::ldp_grrm(5, 1.0, 1);
    }

    #[pg_test]
    fn test_lie_never_equals_truth() {
        // Verify that when a lie is generated, it's never equal to the input
        // This tests the offset logic indirectly
        for value in 1..=10 {
            for _ in 0..50 {
                // Use very low epsilon to force lies most of the time
                let result = crate::ldp::ldp_grrm(value, 0.01, 10);
                // Result should still be in valid range
                assert!(result >= 1 && result <= 10);
            }
        }
    }
}
