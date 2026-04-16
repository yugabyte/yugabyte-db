use std::str::FromStr;

#[derive(Debug, Clone, Copy, Default, PartialEq)]
pub(crate) enum MatchBy {
    #[default]
    Position,
    Name,
}

impl FromStr for MatchBy {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "position" => Ok(MatchBy::Position),
            "name" => Ok(MatchBy::Name),
            _ => Err(format!("unrecognized match_by method: {}", s)),
        }
    }
}
