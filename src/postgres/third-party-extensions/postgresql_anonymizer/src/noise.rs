use rand::Rng;

//----------------------------------------------------------------------------
// GPS Coordinates using the Point type
//----------------------------------------------------------------------------

pub fn noisy_gps_coordinates(
    origin: pgrx::pg_sys::Point,
    radius: f64,
    is_metric: bool,
) -> pgrx::pg_sys::Point {
    // find where we want to go in a circle around origin
    let angle = rand::thread_rng().gen_range(0.0..(2.0 * std::f64::consts::PI));

    // convert distance in kilometers
    let radius_km = if is_metric { radius } else { radius * 1.60934 };
    // we multiply by 0.995 to prevent rounding errors from producing a
    // value over radius
    let distance: f64 = rand::thread_rng().gen_range(0.0..(radius_km)) * 0.995;

    // multiply by 360 degree and divide by the average earth circonference
    // to convert to a distance in degrees
    let distance_in_degree: f64 = distance * 360.0 / 40_041.0;
    let latitude = origin.x + angle.sin() * distance_in_degree;
    let mut longitude = origin.y + angle.cos() * distance_in_degree;

    // Latitude is expressed in degrees between: -90°and +90°
    // Normalize latitude
    let mut latitude = latitude.rem_euclid(360.0);
    // Then fold into [-90, 90]
    if latitude > 90.0 && latitude <= 270.0 {
        // Wrap around the pole -> adjust longitude
        latitude = 180.0 - latitude;
        longitude += 180.0;
    } else if latitude > 270.0 {
        latitude -= 360.0;
    };

    // Longitude is expressed in degrees between: -180°and +180°
    // Note: we could use: longitude = (((longitude % 360.0) + 540.0) % 360.0) - 180.0;
    longitude = longitude.rem_euclid(360.0);
    if longitude > 180.0 {
        longitude -= 360.0;
    }

    // This rounding method has problems with big and tiny numbers. For GPS
    // coordinates, it shouldn't matter since it's clamped between (-180° and
    // 180°) and GPS don't have infinite precision. It also has lots of rounding
    // errors but we don't care since we're adding noise.
    pgrx::pg_sys::Point {
        x: (latitude * 1e6).round() / 1e6,
        y: (longitude * 1e6).round() / 1e6,
    }
}
