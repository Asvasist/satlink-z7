//! Mission definitions of SatLink-Z7 (docs/icd section 5): APIDs, functions, housekeeping
//! structures, events and failure codes, with telecommand builders and report decoders.

use crate::pus::{encode_tc, Telecommand, Telemetry, ACK_ACCEPTANCE, ACK_COMPLETION};
use std::fmt;

pub const APID_PAYLOAD: u16 = 0x010;
pub const GROUND_ID: u16 = 0x0001;
pub const TC_PORT: u16 = 10025;
pub const TM_PORT: u16 = 10026;
pub const NOISE_SCALE: f64 = 4096.0;

pub const MODCOD_NAMES: [&str; 5] = ["BPSK 1/2", "QPSK 1/2", "QPSK 3/4", "8PSK 2/3", "8PSK 5/6"];

/// ST[08] function identifiers.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum Function {
    SetModcod = 1,
    SetAcm = 2,
    SetChannel = 3,
    StartPass = 4,
    StopPass = 5,
    SetLoopback = 6,
    RestartModem = 7,
}

pub fn modcod_name(m: u8) -> String {
    MODCOD_NAMES
        .get(usize::from(m))
        .map_or_else(|| format!("MODCOD {m}"), |s| (*s).to_string())
}

pub fn event_name(id: u16) -> String {
    match id {
        1 => "link locked".into(),
        2 => "link lost".into(),
        3 => "MODCOD changed".into(),
        4 => "AOS".into(),
        5 => "LOS".into(),
        6 => "firmware".into(),
        7 => "modem restarted".into(),
        _ => format!("event {id}"),
    }
}

pub fn failure_name(code: u16) -> String {
    match code {
        1 => "unknown service".into(),
        2 => "unknown subtype".into(),
        3 => "bad application data".into(),
        4 => "unknown function".into(),
        5 => "modem error".into(),
        6 => "wrong APID".into(),
        7 => "corrupt packet".into(),
        _ => format!("failure {code}"),
    }
}

/// Channel emulator noise level for an Es/N0 in dB.
pub fn noise_level_for(esn0_db: f64) -> u16 {
    let sigma = 10f64.powf(-esn0_db / 20.0);
    (sigma * NOISE_SCALE).round().clamp(0.0, 65535.0) as u16
}

fn centi(v: f64) -> [u8; 2] {
    ((v * 100.0).round() as i16).to_be_bytes()
}

/// Builds telecommands with a running 14-bit sequence count.
#[derive(Debug, Default)]
pub struct Commander {
    seq: u16,
}

impl Commander {
    pub fn new() -> Self {
        Self::default()
    }

    /// Starts at @p seq (e.g. derived from the clock, so consecutive runs differ).
    pub fn starting_at(seq: u16) -> Self {
        Self { seq: seq & 0x3FFF }
    }

    /// Sequence count of the next telecommand.
    pub fn next_sequence(&self) -> u16 {
        self.seq
    }

    pub fn build(&mut self, service: u8, subtype: u8, data: Vec<u8>) -> Vec<u8> {
        let tc = Telecommand {
            apid: APID_PAYLOAD,
            sequence_count: self.seq,
            ack_flags: ACK_ACCEPTANCE | ACK_COMPLETION,
            service,
            subtype,
            source_id: GROUND_ID,
            data,
        };
        self.seq = (self.seq + 1) & 0x3FFF;
        encode_tc(&tc)
    }

    fn function(&mut self, f: Function, args: &[u8]) -> Vec<u8> {
        let mut data = vec![f as u8];
        data.extend_from_slice(args);
        self.build(8, 1, data)
    }

    pub fn ping(&mut self) -> Vec<u8> {
        self.build(17, 1, vec![])
    }

    pub fn set_modcod(&mut self, modcod: u8) -> Vec<u8> {
        self.function(Function::SetModcod, &[modcod])
    }

    pub fn set_acm(&mut self, on: bool, min: u8, max: u8, margin_db: f64, hyst_db: f64) -> Vec<u8> {
        let mut a = vec![u8::from(on), min, max];
        a.extend_from_slice(&centi(margin_db));
        a.extend_from_slice(&centi(hyst_db));
        self.function(Function::SetAcm, &a)
    }

    pub fn set_channel(&mut self, noise: u16, gain_q15: u16) -> Vec<u8> {
        let mut a = noise.to_be_bytes().to_vec();
        a.extend_from_slice(&gain_q15.to_be_bytes());
        self.function(Function::SetChannel, &a)
    }

    pub fn start_pass(&mut self, max_el_deg: f64, zenith_db: f64, scale: u8) -> Vec<u8> {
        let mut a = centi(max_el_deg).to_vec();
        a.extend_from_slice(&centi(zenith_db));
        a.push(scale);
        self.function(Function::StartPass, &a)
    }

    pub fn stop_pass(&mut self) -> Vec<u8> {
        self.function(Function::StopPass, &[])
    }

    pub fn set_loopback(&mut self, mode: u8) -> Vec<u8> {
        self.function(Function::SetLoopback, &[mode])
    }

    pub fn restart_modem(&mut self) -> Vec<u8> {
        self.function(Function::RestartModem, &[])
    }

    pub fn enable_hk(&mut self, sids: &[u8], enable: bool) -> Vec<u8> {
        let mut a = vec![sids.len() as u8];
        a.extend_from_slice(sids);
        self.build(3, if enable { 5 } else { 6 }, a)
    }

    pub fn one_shot_hk(&mut self, sids: &[u8]) -> Vec<u8> {
        let mut a = vec![sids.len() as u8];
        a.extend_from_slice(sids);
        self.build(3, 27, a)
    }
}

/// Big-endian cursor over report data.
struct Reader<'a> {
    d: &'a [u8],
    pos: usize,
}

impl<'a> Reader<'a> {
    fn new(d: &'a [u8]) -> Self {
        Self { d, pos: 0 }
    }
    fn take<const N: usize>(&mut self) -> Option<[u8; N]> {
        let bytes = self.d.get(self.pos..self.pos + N)?;
        self.pos += N;
        bytes.try_into().ok()
    }
    fn u8(&mut self) -> Option<u8> {
        self.take::<1>().map(|b| b[0])
    }
    fn u16(&mut self) -> Option<u16> {
        self.take::<2>().map(u16::from_be_bytes)
    }
    fn i16(&mut self) -> Option<i16> {
        self.take::<2>().map(i16::from_be_bytes)
    }
    fn u32(&mut self) -> Option<u32> {
        self.take::<4>().map(u32::from_be_bytes)
    }
    fn done(&self) -> bool {
        self.pos == self.d.len()
    }
}

#[derive(Debug, Clone, PartialEq)]
pub struct ModemHk {
    pub locked: bool,
    pub modcod: u8,
    pub acm: bool,
    pub esn0_db: f64,
    pub frames_ok: u32,
    pub frames_crc_error: u32,
    pub header_errors: u32,
    pub bit_errors: u32,
    pub bits_checked: u32,
    pub latency_max_us: u32,
    pub latency_avg_us: u32,
    pub cpu_load_pct: f64,
    pub tx_queue: u8,
    pub uptime_ms: u32,
}

#[derive(Debug, Clone, PartialEq)]
pub struct LinkHk {
    pub pass_active: bool,
    pub elevation_deg: f64,
    pub range_km: f64,
    pub range_rate_km_s: f64,
    pub pass_esn0_db: f64,
    pub frames_sent: u32,
    pub frames_received: u32,
    pub lost_frames: u32,
    pub packets: u32,
    pub resyncs: u32,
    pub ip_down: u32,
    pub ip_up: u32,
    pub dropped: u32,
}

#[derive(Debug, Clone, PartialEq)]
pub struct PlatformHk {
    pub hkc_valid: bool,
    pub temperature_c: f64,
    pub vccint_mv: u16,
    pub vccaux_mv: u16,
    pub vbram_mv: u16,
    pub hkc_uptime_s: u32,
    pub hkc_error_flags: u8,
    pub rtos_state: u8,
    pub rtos_restarts: u32,
}

/// Housekeeping structure 4: received symbols, unit amplitude = 1.
#[derive(Debug, Clone, PartialEq)]
pub struct ConstellationHk {
    pub modcod: u8,
    pub points: Vec<(f64, f64)>,
}

impl ConstellationHk {
    /// RMS distance of the points from the unit circle (a rough noise figure for PSK).
    pub fn rms_radius_error(&self) -> f64 {
        if self.points.is_empty() {
            return 0.0;
        }
        let sum: f64 = self
            .points
            .iter()
            .map(|(i, q)| (i.hypot(*q) - 1.0).powi(2))
            .sum();
        (sum / self.points.len() as f64).sqrt()
    }
}

#[derive(Debug, Clone, PartialEq)]
pub enum Report {
    Modem(ModemHk),
    Link(LinkHk),
    Platform(PlatformHk),
    Constellation(ConstellationHk),
    Event {
        severity: u8,
        id: u16,
        aux: Vec<u8>,
    },
    Verification {
        subtype: u8,
        sequence_count: u16,
        failure: Option<u16>,
    },
    Pong,
    Unknown {
        service: u8,
        subtype: u8,
    },
}

fn decode_modem(d: &[u8]) -> Option<ModemHk> {
    let mut r = Reader::new(d);
    (r.u8()? == 1).then_some(())?;
    let hk = ModemHk {
        locked: r.u8()? != 0,
        modcod: r.u8()?,
        acm: r.u8()? != 0,
        esn0_db: f64::from(r.i16()?) / 100.0,
        frames_ok: r.u32()?,
        frames_crc_error: r.u32()?,
        header_errors: r.u32()?,
        bit_errors: r.u32()?,
        bits_checked: r.u32()?,
        latency_max_us: r.u32()?,
        latency_avg_us: r.u32()?,
        cpu_load_pct: f64::from(r.u16()?) / 10.0,
        tx_queue: r.u8()?,
        uptime_ms: r.u32()?,
    };
    r.done().then_some(hk)
}

fn decode_link(d: &[u8]) -> Option<LinkHk> {
    let mut r = Reader::new(d);
    (r.u8()? == 2).then_some(())?;
    let hk = LinkHk {
        pass_active: r.u8()? != 0,
        elevation_deg: f64::from(r.i16()?) / 100.0,
        range_km: f64::from(r.u16()?),
        range_rate_km_s: f64::from(r.i16()?) / 1000.0,
        pass_esn0_db: f64::from(r.i16()?) / 100.0,
        frames_sent: r.u32()?,
        frames_received: r.u32()?,
        lost_frames: r.u32()?,
        packets: r.u32()?,
        resyncs: r.u32()?,
        ip_down: r.u32()?,
        ip_up: r.u32()?,
        dropped: r.u32()?,
    };
    r.done().then_some(hk)
}

fn decode_platform(d: &[u8]) -> Option<PlatformHk> {
    let mut r = Reader::new(d);
    (r.u8()? == 3).then_some(())?;
    let hk = PlatformHk {
        hkc_valid: r.u8()? != 0,
        temperature_c: f64::from(r.i16()?) / 100.0,
        vccint_mv: r.u16()?,
        vccaux_mv: r.u16()?,
        vbram_mv: r.u16()?,
        hkc_uptime_s: r.u32()?,
        hkc_error_flags: r.u8()?,
        rtos_state: r.u8()?,
        rtos_restarts: r.u32()?,
    };
    r.done().then_some(hk)
}

fn decode_constellation(d: &[u8]) -> Option<ConstellationHk> {
    let mut r = Reader::new(d);
    (r.u8()? == 4).then_some(())?;
    let modcod = r.u8()?;
    let count = r.u8()?;
    let mut points = Vec::with_capacity(usize::from(count));
    for _ in 0..count {
        let i = f64::from(r.u8()? as i8) / 64.0;
        let q = f64::from(r.u8()? as i8) / 64.0;
        points.push((i, q));
    }
    r.done().then_some(ConstellationHk { modcod, points })
}

/// Interprets a payload TM packet.
pub fn interpret(tm: &Telemetry) -> Report {
    let d = tm.data.as_slice();
    let unknown = Report::Unknown {
        service: tm.service,
        subtype: tm.subtype,
    };
    match (tm.service, tm.subtype) {
        (3, 25) => match d.first() {
            Some(1) => decode_modem(d).map_or(unknown, Report::Modem),
            Some(2) => decode_link(d).map_or(unknown, Report::Link),
            Some(3) => decode_platform(d).map_or(unknown, Report::Platform),
            Some(4) => decode_constellation(d).map_or(unknown, Report::Constellation),
            _ => unknown,
        },
        (5, 1..=4) if d.len() >= 2 => Report::Event {
            severity: tm.subtype,
            id: u16::from_be_bytes([d[0], d[1]]),
            aux: d[2..].to_vec(),
        },
        (1, _) if d.len() == 4 || d.len() == 6 => Report::Verification {
            subtype: tm.subtype,
            sequence_count: u16::from_be_bytes([d[2], d[3]]) & 0x3FFF,
            failure: (d.len() == 6).then(|| u16::from_be_bytes([d[4], d[5]])),
        },
        (17, 2) => Report::Pong,
        _ => unknown,
    }
}

impl fmt::Display for Report {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Report::Modem(h) => write!(
                f,
                "modem: {} {}{} Es/N0 {:.2} dB frames {}/{} crc bits {}/{} load {:.1}% latency {}/{} us",
                if h.locked { "LOCKED" } else { "NO LOCK" },
                modcod_name(h.modcod),
                if h.acm { " (ACM)" } else { "" },
                h.esn0_db,
                h.frames_ok,
                h.frames_crc_error,
                h.bit_errors,
                h.bits_checked,
                h.cpu_load_pct,
                h.latency_max_us,
                h.latency_avg_us
            ),
            Report::Link(h) => write!(
                f,
                "link: pass {} el {:.2} deg range {:.0} km rate {:.3} km/s model {:.2} dB frames tx {} rx {} lost {} ip {}/{}",
                if h.pass_active { "on" } else { "off" },
                h.elevation_deg,
                h.range_km,
                h.range_rate_km_s,
                h.pass_esn0_db,
                h.frames_sent,
                h.frames_received,
                h.lost_frames,
                h.ip_down,
                h.ip_up
            ),
            Report::Platform(h) => write!(
                f,
                "platform: hkc {} {:.2} C vccint {} mV vccaux {} mV vbram {} mV flags 0x{:02x} core1 state {} restarts {}",
                if h.hkc_valid { "ok" } else { "silent" },
                h.temperature_c,
                h.vccint_mv,
                h.vccaux_mv,
                h.vbram_mv,
                h.hkc_error_flags,
                h.rtos_state,
                h.rtos_restarts
            ),
            Report::Constellation(c) => write!(
                f,
                "constellation: {} {} points, RMS radius error {:.3}",
                modcod_name(c.modcod),
                c.points.len(),
                c.rms_radius_error()
            ),
            Report::Event { severity, id, aux } => {
                write!(f, "event[{severity}]: {}", event_name(*id))?;
                match (*id, aux.as_slice()) {
                    (3, [from, to]) => {
                        write!(f, " {} -> {}", modcod_name(*from), modcod_name(*to))
                    }
                    (6, text) if !text.is_empty() => {
                        write!(f, " {}", String::from_utf8_lossy(text))
                    }
                    _ => Ok(()),
                }
            }
            Report::Verification {
                subtype,
                sequence_count,
                failure,
            } => {
                let stage = match subtype {
                    1 | 2 => "acceptance",
                    7 | 8 => "completion",
                    _ => "verification",
                };
                match failure {
                    None => write!(f, "TC {sequence_count}: {stage} OK"),
                    Some(code) => write!(
                        f,
                        "TC {sequence_count}: {stage} FAILED ({})",
                        failure_name(*code)
                    ),
                }
            }
            Report::Pong => f.write_str("alive (TM[17,2])"),
            Report::Unknown { service, subtype } => write!(f, "TM[{service},{subtype}]"),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::pus::{decode_tm, encode_tm};

    fn tm(service: u8, subtype: u8, data: Vec<u8>) -> Telemetry {
        let t = Telemetry {
            apid: APID_PAYLOAD,
            sequence_count: 1,
            service,
            subtype,
            message_counter: 1,
            destination_id: GROUND_ID,
            seconds: 1,
            fraction: 0,
            data,
        };
        decode_tm(&encode_tm(&t)).unwrap()
    }

    #[test]
    fn function_layouts_match_the_icd() {
        let mut c = Commander::new();
        let p = c.set_acm(true, 0, 4, 1.0, 0.5);
        // header 6 + secondary 5, then the function and its arguments
        assert_eq!(&p[11..19], &[2, 1, 0, 4, 0, 100, 0, 50]);
        let p = c.start_pass(80.0, 20.0, 25);
        assert_eq!(&p[11..17], &[4, 0x1F, 0x40, 0x07, 0xD0, 25]);
        let p = c.set_channel(0x0800, 0x7FFF);
        assert_eq!(&p[11..16], &[3, 0x08, 0x00, 0x7F, 0xFF]);
        let p = c.enable_hk(&[1, 3], false);
        assert_eq!((p[7], p[8]), (3, 6));
        assert_eq!(&p[11..14], &[2, 1, 3]);
        assert_eq!(c.next_sequence(), 4);
        assert_eq!(c.set_modcod(3)[11..13], [1, 3]);
        assert_eq!(c.set_loopback(1)[11..13], [6, 1]);
        assert_eq!(c.stop_pass()[11], 5);
        assert_eq!(c.restart_modem()[11], 7);
        assert_eq!(c.one_shot_hk(&[2])[8], 27);
        assert_eq!(c.ping()[7], 17);
        let mut c = Commander::starting_at(0x3FFF);
        assert_eq!(c.ping()[2..4], [0xFF, 0xFF]);
        assert_eq!(c.next_sequence(), 0);
    }

    #[test]
    fn reports_decode() {
        let mut modem = vec![1, 1, 2, 1, 0x04, 0xB0];
        modem.extend(std::iter::repeat(0).take(35));
        let r = interpret(&tm(3, 25, modem.clone()));
        match &r {
            Report::Modem(h) => {
                assert!(h.locked && h.acm);
                assert_eq!(h.modcod, 2);
                assert!((h.esn0_db - 12.0).abs() < 1e-9);
            }
            other => panic!("{other:?}"),
        }
        assert!(r
            .to_string()
            .starts_with("modem: LOCKED QPSK 3/4 (ACM) Es/N0 12.00 dB"));
        modem.push(0);
        assert!(matches!(
            interpret(&tm(3, 25, modem)),
            Report::Unknown { .. }
        ));

        let mut link = vec![2u8, 1, 0x17, 0x70];
        link.extend(std::iter::repeat(0).take(38));
        assert!(
            matches!(interpret(&tm(3, 25, link)), Report::Link(h) if (h.elevation_deg - 60.0).abs() < 1e-9)
        );
        let mut platform = vec![3u8, 1, 0x0B, 0xB8];
        platform.extend(std::iter::repeat(0).take(16));
        let r = interpret(&tm(3, 25, platform));
        assert!(matches!(&r, Report::Platform(h) if (h.temperature_c - 30.0).abs() < 1e-9));
        assert!(r.to_string().contains("30.00 C"));
        assert!(matches!(
            interpret(&tm(3, 25, vec![9])),
            Report::Unknown { .. }
        ));
        let c = interpret(&tm(3, 25, vec![4, 3, 2, 64, 0, 0, 0xC0]));
        match &c {
            Report::Constellation(h) => {
                assert_eq!(h.points, vec![(1.0, 0.0), (0.0, -1.0)]);
                assert!(h.rms_radius_error() < 1e-12);
            }
            other => panic!("{other:?}"),
        }
        assert_eq!(
            c.to_string(),
            "constellation: 8PSK 2/3 2 points, RMS radius error 0.000"
        );
        assert!(matches!(
            interpret(&tm(3, 25, vec![4, 3, 2, 64])),
            Report::Unknown { .. }
        ));

        let e = interpret(&tm(5, 1, vec![0, 3, 1, 2]));
        assert_eq!(
            e.to_string(),
            "event[1]: MODCOD changed QPSK 1/2 -> QPSK 3/4"
        );
        let e = interpret(&tm(5, 2, vec![0, 6, b'h', b'i']));
        assert_eq!(e.to_string(), "event[2]: firmware hi");
        assert_eq!(
            interpret(&tm(5, 1, vec![0, 42])).to_string(),
            "event[1]: event 42"
        );

        let v = interpret(&tm(1, 8, vec![0x18, 0x10, 0xC0, 0x05, 0, 5]));
        assert_eq!(v.to_string(), "TC 5: completion FAILED (modem error)");
        let v = interpret(&tm(1, 1, vec![0x18, 0x10, 0xC0, 0x05]));
        assert_eq!(v.to_string(), "TC 5: acceptance OK");
        assert_eq!(interpret(&tm(17, 2, vec![])), Report::Pong);
        assert_eq!(interpret(&tm(9, 9, vec![])).to_string(), "TM[9,9]");
    }

    #[test]
    fn names() {
        assert_eq!(modcod_name(9), "MODCOD 9");
        assert_eq!(failure_name(3), "bad application data");
        assert_eq!(failure_name(99), "failure 99");
        assert_eq!(event_name(4), "AOS");
        assert_eq!(noise_level_for(0.0), 4096);
        assert_eq!(noise_level_for(10.0), 1295);
    }
}
