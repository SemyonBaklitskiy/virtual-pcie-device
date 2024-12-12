use std::{
    fmt::Display,
    fs,
    num::ParseIntError,
    path::{Path, PathBuf},
};

const HEX: u32 = 16;

#[derive(thiserror::Error, Debug)]
pub enum Error {
    #[error("pci-id:")]
    IO(#[from] std::io::Error),
    #[error("pci-id: Couldn't parse vendor or device file")]
    Parse(#[from] ParseIntError),
    #[error("pci-id: No any supported device was found")]
    NotFound,
}

#[derive(PartialEq, Eq, Clone, Copy, Debug)]
pub struct PciDeviceID {
    pub vendor: u16,
    pub device: u16,
}

impl PciDeviceID {
    pub(crate) fn new(vendor: String, device: String) -> Result<Self, Error> {
        // Parse vendor and device Strings
        let vendor = vendor.strip_prefix("0x").unwrap_or(&vendor).trim_end();
        let device = device.strip_prefix("0x").unwrap_or(&device).trim_end();

        Ok(PciDeviceID {
            vendor: u16::from_str_radix(vendor, HEX)?,
            device: u16::from_str_radix(device, HEX)?,
        })
    }
}

impl Display for PciDeviceID {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{:x} {:x}", self.vendor, self.device)
    }
}

pub fn search(
    supported_ids: &[PciDeviceID],
    path: impl AsRef<Path>,
) -> Result<Vec<(PciDeviceID, PathBuf)>, Error> {
    let mut found = Vec::new();
    for entry in fs::read_dir(path)? {
        let entry = entry?;
        let path = entry.path();

        let pci_device_id = PciDeviceID::new(
            fs::read_to_string(path.join("vendor"))?,
            fs::read_to_string(path.join("device"))?,
        )?;

        if let Some(&id) = supported_ids.iter().find(|&&id| id == pci_device_id) {
            found.push((id, path));
        }
    }

    // Check for any supported device was found
    if found.is_empty() {
        Err(Error::NotFound)
    } else {
        Ok(found)
    }
}
