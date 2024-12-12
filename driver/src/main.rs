/////////////////////////////////////нужные крейты///////////////////////////////////////////////////
use std::{
    fs::{self},
    io::ErrorKind,
    path::{Path, PathBuf},
};

use pci_driver::{
    backends::vfio::VfioPciDevice, device::PciDevice, regions::MappedOwningPciRegion,
};
//use pci_id::PciDeviceID;
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

use std::{fmt::Display, num::ParseIntError};

const HEX: u32 = 16;

#[derive(thiserror::Error, Debug)]
pub enum Error {
    #[error("pci-id:")]
    IO(#[from] std::io::Error),
    #[error("pci-id: Couldn't parse vendor or device file")]
    Parse(#[from] ParseIntError),
    #[error("pci-id: No any supported device was found")]
    NotFound,
    #[error("binding: vfio-pci is not loaded")]
    NotLoaded,
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

const PCI_DEVICES_PATH: &str = "/sys/bus/pci/devices";

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

///////////////////////////////////////////////////////////////////////////////////////////////////////
use std::{
    hint,
    time::{Duration, Instant},
};

use tock_registers::{
    interfaces::{Readable, Writeable},
    register_bitfields, register_structs,
    registers::{ReadOnly, ReadWrite},
};
#[allow(dead_code)]
struct InferenceDevice {
    region0: MappedOwningPciRegion,
    region1: MappedOwningPciRegion,
    region2: MappedOwningPciRegion,
    register_space: &'static RegisterSpace,
    input_data: &'static [u8; 4096], //раскур плотный
    output_data: &'static [u8; 4096],
}

register_structs! {
    pub RegisterSpace{
        (0x00   => control: ReadWrite<u32, Control::Register>),
        (0x04   => control_w1s: ReadWrite<u32, Control::Register>),
        (0x08   => control_w1c: ReadWrite<u32, Control::Register>),
        (0x0C   => status: ReadOnly<u32, Status::Register>),
        (0x10   => _reserved),
        (0x40   => @END),
    }

}

register_bitfields![u32,
    pub Control [
        START OFFSET(0) NUMBITS(1),
        STOP OFFSET(1) NUMBITS(1),
        RESET OFFSET(2) NUMBITS(1)
    ],

    pub Status[
        BUSY OFFSET(0) NUMBITS(1),
        DONE OFFSET(1) NUMBITS(1),
        ERROR OFFSET(2) NUMBITS(4)

    ]

];

register_bitfields![u8,
    pub DATA [
        RESET OFFSET(0) NUMBITS(8)
    ]
];

impl InferenceDevice {
    fn new() -> Option<Self> {
        let searched = search(
            &[PciDeviceID {
                vendor: 0x1234,
                device: 0xcafe,
            }],
            PCI_DEVICES_PATH,
        )
        .unwrap();
        let (id, path) = searched.first().unwrap();
        let binder = BindedDevice::new(*id, path.to_path_buf()).unwrap();
        let device = binder.get_vfio_pci_device();
        let mappedbar0 = device
            .bar(0)?
            .map(0..64, pci_driver::regions::Permissions::ReadWrite)
            .unwrap();

        let ptr0 = mappedbar0.as_ptr().cast::<RegisterSpace>();
        let reg_space0 = unsafe { ptr0.as_ref()? }; //зуб даю

        let mappedbar1 = device
            .bar(1)?
            .map(0..4096, pci_driver::regions::Permissions::ReadWrite)
            .unwrap();

        let ptr1 = mappedbar1.as_ptr().cast::<[u8; 4096]>();
        let input_memory = unsafe { ptr1.as_ref()? }; //зуб даю

        let mappedbar2 = device
            .bar(0)?
            .map(0..4096, pci_driver::regions::Permissions::Read)
            .unwrap();

        let ptr2 = mappedbar2.as_ptr().cast::<[u8; 4096]>();
        let output_memory = unsafe { ptr2.as_ref()? }; //зуб даю

        let result = Self {
            region0: mappedbar0,
            region1: mappedbar1,
            region2: mappedbar2,
            register_space: reg_space0,
            input_data: input_memory,
            output_data: output_memory,
        };
        result.reset();
        Some(result)
    }
    fn reset(&self) {
        self.register_space.control.write(Control::RESET::SET);

    }
    fn do_inference(&self) -> Result<(), ()> {
        let wait_until = Instant::now() + Duration::from_millis(1000);
        self.register_space.control.write(Control::START::SET);
        while wait_until > Instant::now() {
            if !self.register_space.status.is_set(Status::DONE) {
                return Ok(());
            }
            hint::spin_loop();
        }
        Err(())
    }
    pub fn is_done(&self) -> bool {
        self.register_space.status.is_set(Status::DONE)
    }
}

fn main() {
    let  device = InferenceDevice::new().unwrap();
    device.do_inference().unwrap();
    device.is_done();

}
