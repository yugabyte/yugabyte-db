use eyre::{WrapErr, bail};
use object::read::macho::{FatArch, MachOFatFile32, MachOFatFile64};
use object::{Object, ObjectSection};
use pgrx_sql_entity_graph::section::{
    MACHO_SECTION_NAME, MACHO_SEGMENT_NAME, is_schema_section_name,
};

#[derive(Clone, Copy)]
enum MachBits {
    Bits32,
    Bits64,
}

#[derive(Clone, Copy)]
enum MachEndian {
    Little,
    Big,
}

pub(crate) fn schema_section_data(data: &[u8]) -> eyre::Result<Option<&[u8]>> {
    let kind = object::FileKind::parse(data).wrap_err("couldn't parse binary kind")?;

    match kind {
        object::FileKind::MachOFat32 => match slice_arch32(data, target_architecture()) {
            Some(slice) => schema_section_data(slice),
            None => Ok(None),
        },
        object::FileKind::MachOFat64 => match slice_arch64(data, target_architecture()) {
            Some(slice) => schema_section_data(slice),
            None => Ok(None),
        },
        object::FileKind::MachO32 | object::FileKind::MachO64 => macho_schema_section_data(data),
        _ => schema_section_data_from_object(data),
    }
}

fn schema_section_data_from_object(data: &[u8]) -> eyre::Result<Option<&[u8]>> {
    let object = object::File::parse(data).wrap_err("couldn't parse binary object")?;

    for section in object.sections() {
        let name = section.name().wrap_err("couldn't read section name")?;
        if is_schema_section_name(name) {
            return section.data().wrap_err("couldn't read pgrx schema section").map(Some);
        }
    }

    Ok(None)
}

fn macho_schema_section_data(data: &[u8]) -> eyre::Result<Option<&[u8]>> {
    let (bits, endian, header_size) = parse_macho_header(data)?;
    let ncmds = read_u32(data, 16, endian)?;
    let mut cursor = header_size;

    for _ in 0..ncmds {
        let cmd = read_u32(data, cursor, endian)?;
        let cmdsize = read_u32(data, cursor + 4, endian)? as usize;
        if cmdsize < 8 {
            bail!("invalid Mach-O load command size");
        }

        let command_end = cursor
            .checked_add(cmdsize)
            .filter(|end| *end <= data.len())
            .ok_or_else(|| eyre::eyre!("invalid Mach-O load command range"))?;
        let command = &data[cursor..command_end];

        let section = match bits {
            MachBits::Bits32 if cmd == object::macho::LC_SEGMENT => {
                macho_schema_section_from_segment32(command, data, endian)?
            }
            MachBits::Bits64 if cmd == object::macho::LC_SEGMENT_64 => {
                macho_schema_section_from_segment64(command, data, endian)?
            }
            _ => None,
        };

        if section.is_some() {
            return Ok(section);
        }

        cursor = command_end;
    }

    Ok(None)
}

fn macho_schema_section_from_segment32<'a>(
    command: &'a [u8],
    data: &'a [u8],
    endian: MachEndian,
) -> eyre::Result<Option<&'a [u8]>> {
    const SEGMENT_LEN: usize = 56;
    const SECTION_LEN: usize = 68;

    if command.len() < SEGMENT_LEN {
        bail!("invalid Mach-O 32-bit segment command");
    }

    if trim_nul(&command[8..24]) != MACHO_SEGMENT_NAME.as_bytes() {
        return Ok(None);
    }

    let nsects = read_u32(command, 48, endian)? as usize;
    let sections = &command[SEGMENT_LEN..];
    let expected_len = nsects
        .checked_mul(SECTION_LEN)
        .ok_or_else(|| eyre::eyre!("invalid Mach-O 32-bit section count"))?;
    if sections.len() < expected_len {
        bail!("invalid Mach-O 32-bit section table");
    }

    for index in 0..nsects {
        let start = index * SECTION_LEN;
        let section = &sections[start..start + SECTION_LEN];
        if trim_nul(&section[..16]) == MACHO_SECTION_NAME.as_bytes()
            && trim_nul(&section[16..32]) == MACHO_SEGMENT_NAME.as_bytes()
        {
            let size = read_u32(section, 36, endian)? as u64;
            let offset = read_u32(section, 40, endian)? as u64;
            return slice_range(data, offset, size).map(Some);
        }
    }

    Ok(None)
}
fn macho_schema_section_from_segment64<'a>(
    command: &'a [u8],
    data: &'a [u8],
    endian: MachEndian,
) -> eyre::Result<Option<&'a [u8]>> {
    const SEGMENT_LEN: usize = 72;
    const SECTION_LEN: usize = 80;

    if command.len() < SEGMENT_LEN {
        bail!("invalid Mach-O 64-bit segment command");
    }

    if trim_nul(&command[8..24]) != MACHO_SEGMENT_NAME.as_bytes() {
        return Ok(None);
    }

    let nsects = read_u32(command, 64, endian)? as usize;
    let sections = &command[SEGMENT_LEN..];
    let expected_len = nsects
        .checked_mul(SECTION_LEN)
        .ok_or_else(|| eyre::eyre!("invalid Mach-O 64-bit section count"))?;
    if sections.len() < expected_len {
        bail!("invalid Mach-O 64-bit section table");
    }

    for index in 0..nsects {
        let start = index * SECTION_LEN;
        let section = &sections[start..start + SECTION_LEN];
        if trim_nul(&section[..16]) == MACHO_SECTION_NAME.as_bytes()
            && trim_nul(&section[16..32]) == MACHO_SEGMENT_NAME.as_bytes()
        {
            let size = read_u64(section, 40, endian)?;
            let offset = read_u32(section, 48, endian)? as u64;
            return slice_range(data, offset, size).map(Some);
        }
    }

    Ok(None)
}

fn parse_macho_header(data: &[u8]) -> eyre::Result<(MachBits, MachEndian, usize)> {
    let magic = data.get(..4).ok_or_else(|| eyre::eyre!("Mach-O file is too small"))?;

    match u32::from_be_bytes(magic.try_into().expect("already bounds-checked to 4 bytes")) {
        object::macho::MH_CIGAM => Ok((MachBits::Bits32, MachEndian::Little, 28)),
        object::macho::MH_MAGIC => Ok((MachBits::Bits32, MachEndian::Big, 28)),
        object::macho::MH_CIGAM_64 => Ok((MachBits::Bits64, MachEndian::Little, 32)),
        object::macho::MH_MAGIC_64 => Ok((MachBits::Bits64, MachEndian::Big, 32)),
        _ => bail!("invalid Mach-O magic"),
    }
}

fn read_u32(data: &[u8], offset: usize, endian: MachEndian) -> eyre::Result<u32> {
    let bytes =
        data.get(offset..offset + 4).ok_or_else(|| eyre::eyre!("unexpected end of Mach-O data"))?;
    let bytes: [u8; 4] = bytes.try_into().expect("already bounds-checked to 4 bytes");

    Ok(match endian {
        MachEndian::Little => u32::from_le_bytes(bytes),
        MachEndian::Big => u32::from_be_bytes(bytes),
    })
}

fn read_u64(data: &[u8], offset: usize, endian: MachEndian) -> eyre::Result<u64> {
    let bytes =
        data.get(offset..offset + 8).ok_or_else(|| eyre::eyre!("unexpected end of Mach-O data"))?;
    let bytes: [u8; 8] = bytes.try_into().expect("already bounds-checked to 8 bytes");

    Ok(match endian {
        MachEndian::Little => u64::from_le_bytes(bytes),
        MachEndian::Big => u64::from_be_bytes(bytes),
    })
}

fn slice_range(data: &[u8], offset: u64, size: u64) -> eyre::Result<&[u8]> {
    let end =
        offset.checked_add(size).ok_or_else(|| eyre::eyre!("invalid Mach-O section range"))?;
    let start = usize::try_from(offset).wrap_err("Mach-O section offset overflowed usize")?;
    let end = usize::try_from(end).wrap_err("Mach-O section end overflowed usize")?;

    data.get(start..end).ok_or_else(|| eyre::eyre!("invalid Mach-O section bounds"))
}

fn trim_nul(bytes: &[u8]) -> &[u8] {
    let len = bytes.iter().position(|byte| *byte == 0).unwrap_or(bytes.len());
    &bytes[..len]
}

fn target_architecture() -> object::Architecture {
    match std::env::consts::ARCH {
        "x86" => object::Architecture::I386,
        "x86_64" => object::Architecture::X86_64,
        "arm" => object::Architecture::Arm,
        "aarch64" => object::Architecture::Aarch64,
        "mips" => object::Architecture::Mips,
        "powerpc" => object::Architecture::PowerPc,
        "powerpc64" => object::Architecture::PowerPc64,
        _ => object::Architecture::Unknown,
    }
}

fn slice_arch32(data: &[u8], arch: object::Architecture) -> Option<&[u8]> {
    let candidates = MachOFatFile32::parse(data).ok()?;
    let architecture =
        candidates.arches().iter().find(|candidate| candidate.architecture() == arch)?;

    architecture.data(data).ok()
}

fn slice_arch64(data: &[u8], arch: object::Architecture) -> Option<&[u8]> {
    let candidates = MachOFatFile64::parse(data).ok()?;
    let architecture =
        candidates.arches().iter().find(|candidate| candidate.architecture() == arch)?;

    architecture.data(data).ok()
}

#[cfg(test)]
mod tests {
    use super::schema_section_data;
    use object::read::macho::{FatArch, MachOFatFile32};
    use pgrx_pg_config::{PgConfigSelector, Pgrx};
    use pgrx_sql_entity_graph::section::{
        MACHO_SECTION_NAME, MACHO_SEGMENT_NAME, schema_section_sentinel_entry,
    };

    fn parse_object(data: &[u8]) -> object::Result<object::File<'_>> {
        let kind = object::FileKind::parse(data)?;

        match kind {
            object::FileKind::MachOFat32 => {
                let arch = std::env::consts::ARCH;

                match slice_arch32(data, arch) {
                    Some(slice) => parse_object(slice),
                    None => {
                        panic!("Failed to slice architecture '{arch}' from universal binary.")
                    }
                }
            }
            _ => object::File::parse(data),
        }
    }

    fn slice_arch32<'a>(data: &'a [u8], arch: &str) -> Option<&'a [u8]> {
        use object::Architecture;

        let target = match arch {
            "x86" => Architecture::I386,
            "x86_64" => Architecture::X86_64,
            "arm" => Architecture::Arm,
            "aarch64" => Architecture::Aarch64,
            "mips" => Architecture::Mips,
            "powerpc" => Architecture::PowerPc,
            "powerpc64" => Architecture::PowerPc64,
            _ => Architecture::Unknown,
        };

        let candidates = MachOFatFile32::parse(data).ok()?;
        let architecture = candidates.arches().iter().find(|a| a.architecture() == target)?;

        architecture.data(data).ok()
    }

    fn minimal_macho64(payload: &[u8]) -> Vec<u8> {
        const HEADER_LEN: usize = 32;
        const SEGMENT_LEN: usize = 72;
        const SECTION_LEN: usize = 80;
        let command_len = SEGMENT_LEN + SECTION_LEN;
        let fileoff = (HEADER_LEN + command_len) as u64;

        fn push_padded_name(bytes: &mut Vec<u8>, name: &str, width: usize) {
            assert!(name.len() <= width, "name `{name}` must fit in {width} bytes");
            bytes.extend_from_slice(name.as_bytes());
            bytes.resize(bytes.len() + (width - name.len()), 0);
        }

        let mut bytes = Vec::new();
        bytes.extend_from_slice(&[0xcf, 0xfa, 0xed, 0xfe]);
        bytes.extend_from_slice(&0u32.to_le_bytes());
        bytes.extend_from_slice(&0u32.to_le_bytes());
        bytes.extend_from_slice(&object::macho::MH_DYLIB.to_le_bytes());
        bytes.extend_from_slice(&1u32.to_le_bytes());
        bytes.extend_from_slice(&(command_len as u32).to_le_bytes());
        bytes.extend_from_slice(&0u32.to_le_bytes());
        bytes.extend_from_slice(&0u32.to_le_bytes());

        bytes.extend_from_slice(&object::macho::LC_SEGMENT_64.to_le_bytes());
        bytes.extend_from_slice(&(command_len as u32).to_le_bytes());
        push_padded_name(&mut bytes, MACHO_SEGMENT_NAME, 16);
        bytes.extend_from_slice(&0u64.to_le_bytes());
        bytes.extend_from_slice(&(payload.len() as u64).to_le_bytes());
        bytes.extend_from_slice(&fileoff.to_le_bytes());
        bytes.extend_from_slice(&(payload.len() as u64).to_le_bytes());
        bytes.extend_from_slice(&0u32.to_le_bytes());
        bytes.extend_from_slice(&0u32.to_le_bytes());
        bytes.extend_from_slice(&1u32.to_le_bytes());
        bytes.extend_from_slice(&0u32.to_le_bytes());

        push_padded_name(&mut bytes, MACHO_SECTION_NAME, 16);
        push_padded_name(&mut bytes, MACHO_SEGMENT_NAME, 16);
        bytes.extend_from_slice(&0u64.to_le_bytes());
        bytes.extend_from_slice(&(payload.len() as u64).to_le_bytes());
        bytes.extend_from_slice(&(fileoff as u32).to_le_bytes());
        bytes.extend_from_slice(&0u32.to_le_bytes());
        bytes.extend_from_slice(&0u32.to_le_bytes());
        bytes.extend_from_slice(&0u32.to_le_bytes());
        bytes.extend_from_slice(&0u32.to_le_bytes());
        bytes.extend_from_slice(&0u32.to_le_bytes());
        bytes.extend_from_slice(&0u32.to_le_bytes());
        bytes.extend_from_slice(&0u32.to_le_bytes());

        bytes.extend_from_slice(payload);
        bytes
    }

    #[test]
    fn reads_schema_section_from_minimal_macho64() {
        const PAYLOAD: &[u8] = b"\x04\x00\x00\x00test";
        let bytes = minimal_macho64(PAYLOAD);

        assert_eq!(schema_section_data(&bytes).unwrap(), Some(PAYLOAD));
    }

    #[test]
    fn reads_sentinel_schema_section_from_minimal_macho64() {
        let payload = schema_section_sentinel_entry();
        let bytes = minimal_macho64(&payload);

        assert_eq!(schema_section_data(&bytes).unwrap(), Some(payload.as_slice()));
    }

    #[test]
    fn returns_none_for_valid_binary_without_schema_section() {
        let root_path = env!("CARGO_MANIFEST_DIR");
        let fixture_path = format!("{root_path}/tests/fixtures/macos-universal-binary");
        let bin = std::fs::read(fixture_path).unwrap();

        assert!(schema_section_data(&bin).unwrap().is_none());
    }

    #[test]
    fn parses_managed_postmasters() {
        let pgrx = Pgrx::from_config().unwrap();
        let mut results = pgrx
            .iter(PgConfigSelector::All)
            .map(|pg_config| {
                let fixture_path = pg_config.unwrap().postmaster_path().unwrap();
                let bin = std::fs::read(fixture_path).unwrap();

                parse_object(&bin).is_ok()
            })
            .peekable();

        assert!(results.peek().is_some());
        assert!(results.all(|r| r));
    }

    #[test]
    fn parses_universal_binary_slice() {
        let root_path = env!("CARGO_MANIFEST_DIR");
        let fixture_path = format!("{root_path}/tests/fixtures/macos-universal-binary");
        let bin = std::fs::read(fixture_path).unwrap();

        let slice = slice_arch32(&bin, "aarch64")
            .expect("Failed to slice architecture 'aarch64' from universal binary.");
        assert!(parse_object(slice).is_ok());
    }

    #[test]
    fn slice_unknown_architecture_returns_none() {
        let root_path = env!("CARGO_MANIFEST_DIR");
        let fixture_path = format!("{root_path}/tests/fixtures/macos-universal-binary");
        let bin = std::fs::read(fixture_path).unwrap();

        assert!(slice_arch32(&bin, "foo").is_none());
    }
}
