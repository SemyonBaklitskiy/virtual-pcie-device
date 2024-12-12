use std::{
    fs::{self},
    io::ErrorKind,
    path::{Path, PathBuf},
};

use pci_driver::backends::vfio::VfioPciDevice;
use pci_id::PciDeviceID;

#[derive(thiserror::Error, Debug)]
pub enum Error {
    #[error("binding: vfio-pci is not loaded")]
    NotLoaded,
    #[error("binding:")]
    IO(#[from] std::io::Error),
}

const VFIO_DRIVER_PATH: &str = "/sys/bus/pci/drivers/vfio-pci";

pub struct BindedDevice {
    device: VfioPciDevice,
    id: PciDeviceID,
}

impl BindedDevice {
    pub fn new(id: PciDeviceID, sysfs_path: PathBuf) -> Result<Self, Error> {
        // Check for vfio-pci module is loaded
        if !Path::new(VFIO_DRIVER_PATH).try_exists()? {
            return Err(Error::NotLoaded);
        }

        match fs::write(Path::new(VFIO_DRIVER_PATH).join("new_id"), id.to_string()) {
            Ok(()) => Ok(VfioPciDevice::open(sysfs_path)?),
            Err(err) => match err.kind() {
                ErrorKind::AlreadyExists => Ok(VfioPciDevice::open(sysfs_path)?),
                _ => Err(err.into()),
            },
        }
        .map(|device| Self { device, id })
    }

    pub fn get_vfio_pci_device(&self) -> &VfioPciDevice {
        &self.device
    }
}

impl Drop for BindedDevice {
    fn drop(&mut self) {
        fs::write(
            Path::new(VFIO_DRIVER_PATH).join("remove_id"),
            self.id.to_string(),
        )
        // There can't be panic here because of the Linux guarantees
        .expect("I/O error")
    }
}
