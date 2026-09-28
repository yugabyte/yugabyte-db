use crate::{BOX, CIRCLE, Point};

impl PartialEq for Point {
    #[inline]
    fn eq(&self, other: &Self) -> bool {
        self.x == other.x && self.y == other.y
    }
}
impl Eq for Point {}

impl PartialEq for BOX {
    #[inline]
    fn eq(&self, other: &Self) -> bool {
        self.high == other.high && self.low == other.low
    }
}
impl Eq for BOX {}

impl PartialEq for CIRCLE {
    #[inline]
    fn eq(&self, other: &Self) -> bool {
        self.center == other.center && self.radius == other.radius
    }
}
impl Eq for CIRCLE {}
