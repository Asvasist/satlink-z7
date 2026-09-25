//! CCSDS space packets with PUS-C secondary headers (ECSS-E-ST-70-41C).

use crate::crc::crc16_ccitt;
use std::fmt;

pub const PRIMARY_HEADER: usize = 6;
pub const TC_SECONDARY_HEADER: usize = 5;
pub const TM_SECONDARY_HEADER: usize = 13;
pub const PUS_VERSION: u8 = 2;
/// Unix time of the mission epoch 2000-01-01T00:00:00Z.
pub const MISSION_EPOCH_UNIX: u64 = 946_684_800;

pub const ACK_ACCEPTANCE: u8 = 0x1;
pub const ACK_COMPLETION: u8 = 0x8;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Telecommand {
    pub apid: u16,
    pub sequence_count: u16,
    pub ack_flags: u8,
    pub service: u8,
    pub subtype: u8,
    pub source_id: u16,
    pub data: Vec<u8>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Telemetry {
    pub apid: u16,
    pub sequence_count: u16,
    pub service: u8,
    pub subtype: u8,
    pub message_counter: u16,
    pub destination_id: u16,
    /// CUC: seconds and 2^-16 fractions since the mission epoch.
    pub seconds: u32,
    pub fraction: u16,
    pub data: Vec<u8>,
}

impl Telemetry {
    /// Packet time as Unix milliseconds.
    pub fn unix_ms(&self) -> u64 {
        (MISSION_EPOCH_UNIX + u64::from(self.seconds)) * 1000
            + (u64::from(self.fraction) * 1000 + 32768) / 65536
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DecodeError {
    TooShort,
    BadVersion,
    LengthMismatch,
    WrongType,
    NoSecondaryHeader,
    BadPusVersion,
    Crc,
}

impl fmt::Display for DecodeError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let text = match self {
            DecodeError::TooShort => "too short",
            DecodeError::BadVersion => "bad packet version",
            DecodeError::LengthMismatch => "length mismatch",
            DecodeError::WrongType => "wrong packet type",
            DecodeError::NoSecondaryHeader => "no secondary header",
            DecodeError::BadPusVersion => "bad PUS version",
            DecodeError::Crc => "CRC error",
        };
        f.write_str(text)
    }
}

impl std::error::Error for DecodeError {}

fn primary_header(tc: bool, apid: u16, seq: u16, data_len: usize) -> [u8; PRIMARY_HEADER] {
    let word0 = (u16::from(tc) << 12) | 0x0800 | (apid & 0x7FF);
    let word1 = 0xC000 | (seq & 0x3FFF);
    let len = (data_len - 1) as u16;
    let mut h = [0u8; PRIMARY_HEADER];
    h[0..2].copy_from_slice(&word0.to_be_bytes());
    h[2..4].copy_from_slice(&word1.to_be_bytes());
    h[4..6].copy_from_slice(&len.to_be_bytes());
    h
}

/// Encodes a telecommand, CRC included.
pub fn encode_tc(tc: &Telecommand) -> Vec<u8> {
    let data_len = TC_SECONDARY_HEADER + tc.data.len() + 2;
    let mut p = Vec::with_capacity(PRIMARY_HEADER + data_len);
    p.extend_from_slice(&primary_header(true, tc.apid, tc.sequence_count, data_len));
    p.push((PUS_VERSION << 4) | (tc.ack_flags & 0x0F));
    p.push(tc.service);
    p.push(tc.subtype);
    p.extend_from_slice(&tc.source_id.to_be_bytes());
    p.extend_from_slice(&tc.data);
    let crc = crc16_ccitt(&p);
    p.extend_from_slice(&crc.to_be_bytes());
    p
}

/// Encodes a telemetry packet (used by the tests and simulators).
pub fn encode_tm(tm: &Telemetry) -> Vec<u8> {
    let data_len = TM_SECONDARY_HEADER + tm.data.len() + 2;
    let mut p = Vec::with_capacity(PRIMARY_HEADER + data_len);
    p.extend_from_slice(&primary_header(false, tm.apid, tm.sequence_count, data_len));
    p.push(PUS_VERSION << 4);
    p.push(tm.service);
    p.push(tm.subtype);
    p.extend_from_slice(&tm.message_counter.to_be_bytes());
    p.extend_from_slice(&tm.destination_id.to_be_bytes());
    p.extend_from_slice(&tm.seconds.to_be_bytes());
    p.extend_from_slice(&tm.fraction.to_be_bytes());
    p.extend_from_slice(&tm.data);
    let crc = crc16_ccitt(&p);
    p.extend_from_slice(&crc.to_be_bytes());
    p
}

fn be16(p: &[u8], off: usize) -> u16 {
    u16::from_be_bytes([p[off], p[off + 1]])
}

/// Decodes and checks a telemetry packet.
pub fn decode_tm(p: &[u8]) -> Result<Telemetry, DecodeError> {
    if p.len() < PRIMARY_HEADER {
        return Err(DecodeError::TooShort);
    }
    if p[0] >> 5 != 0 {
        return Err(DecodeError::BadVersion);
    }
    let word0 = be16(p, 0);
    if PRIMARY_HEADER + usize::from(be16(p, 4)) + 1 != p.len() {
        return Err(DecodeError::LengthMismatch);
    }
    if word0 & 0x1000 != 0 {
        return Err(DecodeError::WrongType);
    }
    if word0 & 0x0800 == 0 {
        return Err(DecodeError::NoSecondaryHeader);
    }
    if p.len() < PRIMARY_HEADER + TM_SECONDARY_HEADER + 2 {
        return Err(DecodeError::TooShort);
    }
    if p[PRIMARY_HEADER] >> 4 != PUS_VERSION {
        return Err(DecodeError::BadPusVersion);
    }
    if crc16_ccitt(p) != 0 {
        return Err(DecodeError::Crc);
    }
    let s = PRIMARY_HEADER;
    Ok(Telemetry {
        apid: word0 & 0x7FF,
        sequence_count: be16(p, 2) & 0x3FFF,
        service: p[s + 1],
        subtype: p[s + 2],
        message_counter: be16(p, s + 3),
        destination_id: be16(p, s + 5),
        seconds: u32::from_be_bytes([p[s + 7], p[s + 8], p[s + 9], p[s + 10]]),
        fraction: be16(p, s + 11),
        data: p[s + TM_SECONDARY_HEADER..p.len() - 2].to_vec(),
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    /// TC[17,1] to APID 0x010, sequence 0, source 1, acknowledgement 0x9, as produced by the
    /// flight library (satlink::pus::Encode).
    const PING: [u8; 13] = [
        0x18, 0x10, 0xC0, 0x00, 0x00, 0x06, 0x29, 0x11, 0x01, 0x00, 0x01, 0, 0,
    ];

    #[test]
    fn telecommand_layout() {
        let tc = Telecommand {
            apid: 0x010,
            sequence_count: 0,
            ack_flags: ACK_ACCEPTANCE | ACK_COMPLETION,
            service: 17,
            subtype: 1,
            source_id: 1,
            data: vec![],
        };
        let p = encode_tc(&tc);
        assert_eq!(&p[..11], &PING[..11]);
        assert_eq!(crc16_ccitt(&p), 0);
    }

    #[test]
    fn telemetry_round_trip() {
        let tm = Telemetry {
            apid: 0x010,
            sequence_count: 0x3FFF,
            service: 3,
            subtype: 25,
            message_counter: 7,
            destination_id: 1,
            seconds: 815_000_000,
            fraction: 0x8000,
            data: vec![1, 2, 3],
        };
        let p = encode_tm(&tm);
        assert_eq!(p.len(), 6 + 13 + 3 + 2);
        assert_eq!(decode_tm(&p), Ok(tm.clone()));
        assert_eq!(tm.unix_ms(), (946_684_800 + 815_000_000) * 1000 + 500);

        let mut bad = p.clone();
        bad[20] ^= 1;
        assert_eq!(decode_tm(&bad), Err(DecodeError::Crc));
        assert_eq!(decode_tm(&p[..10]), Err(DecodeError::LengthMismatch));
        assert_eq!(decode_tm(&p[..3]), Err(DecodeError::TooShort));
        let mut tc = p.clone();
        tc[0] |= 0x10;
        assert_eq!(decode_tm(&tc), Err(DecodeError::WrongType));
        let mut version = p.clone();
        version[0] |= 0x20;
        assert_eq!(decode_tm(&version), Err(DecodeError::BadVersion));
        let mut pus = p;
        pus[6] = 0x10;
        assert_eq!(decode_tm(&pus), Err(DecodeError::BadPusVersion));
        assert_eq!(DecodeError::Crc.to_string(), "CRC error");
    }
}
