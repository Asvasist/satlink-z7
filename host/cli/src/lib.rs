//! SatLink-Z7 command-line ground segment.
//!
//! The packet layouts are those of the flight software (`libs/pus`, `linux/payload`,
//! docs/icd section 5); the unit tests check them against byte vectors produced by it.
//!
//! @implements SRS-GS-003

pub mod crc;
pub mod mission;
pub mod pus;
