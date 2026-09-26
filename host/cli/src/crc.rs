//! CRC-16-CCITT (polynomial 0x1021, initial value 0xFFFF, no reflection), as used by the
//! packet error control field of PUS packets.

/// CRC of `data` starting from 0xFFFF.
pub fn crc16_ccitt(data: &[u8]) -> u16 {
    crc16_ccitt_update(0xFFFF, data)
}

/// Continues a CRC over more data.
pub fn crc16_ccitt_update(mut crc: u16, data: &[u8]) -> u16 {
    for &byte in data {
        crc ^= u16::from(byte) << 8;
        for _ in 0..8 {
            crc = if crc & 0x8000 != 0 {
                (crc << 1) ^ 0x1021
            } else {
                crc << 1
            };
        }
    }
    crc
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn check_value() {
        // CRC-16/CCITT-FALSE check value.
        assert_eq!(crc16_ccitt(b"123456789"), 0x29B1);
    }

    #[test]
    fn crc_over_packet_with_crc_is_zero() {
        let mut data = b"SatLink".to_vec();
        let crc = crc16_ccitt(&data);
        data.extend_from_slice(&crc.to_be_bytes());
        assert_eq!(crc16_ccitt(&data), 0);
    }
}
