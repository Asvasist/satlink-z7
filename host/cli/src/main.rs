//! `satlink`: command-line ground segment for the SatLink-Z7 payload.
//!
//! ```text
//! satlink [--payload HOST[:PORT]] [--tm-port N] [--timeout S] <command>
//!
//!   ping                               are-you-alive (ST[17])
//!   modcod <0..4>                      fixed MODCOD, ACM off
//!   acm on|off [--min N] [--max N] [--margin DB] [--hyst DB]
//!   channel <Es/N0 dB> | clear         channel emulator
//!   pass start [--el DEG] [--zenith DB] [--scale X] | stop
//!   loopback analog|digital|software
//!   restart                            restart the Core 1 firmware
//!   hk [--count N] [--sid N]           print housekeeping reports
//!   monitor [--seconds S]              print every report
//! ```
//!
//! Commands wait for the ST[01] acceptance and completion reports and exit with status 1 if
//! the payload rejects the command, 2 on a timeout.
//!
//! @implements SRS-GS-003

use satlink_cli::mission::{self, interpret, Commander, Report};
use satlink_cli::pus::decode_tm;
use std::net::{SocketAddr, ToSocketAddrs, UdpSocket};
use std::process::ExitCode;
use std::time::{Duration, Instant};

const USAGE: &str = "usage: satlink [--payload HOST[:PORT]] [--tm-port N] [--timeout S] <command>

commands:
  ping                                  are-you-alive (ST[17])
  modcod <0..4>                         fixed MODCOD, ACM off
  acm on|off [--min N] [--max N] [--margin DB] [--hyst DB]
  channel <Es/N0 dB> | clear            channel emulator
  pass start [--el DEG] [--zenith DB] [--scale X] | stop
  loopback analog|digital|software
  restart                               restart the Core 1 firmware
  hk [--count N] [--sid N]              print housekeeping reports
  monitor [--seconds S]                 print every report";

struct Args {
    words: Vec<String>,
}

impl Args {
    /// Removes `--name VALUE` and returns VALUE.
    fn option(&mut self, name: &str) -> Result<Option<String>, String> {
        match self.words.iter().position(|w| w == name) {
            None => Ok(None),
            Some(i) if i + 1 < self.words.len() => {
                let v = self.words.remove(i + 1);
                self.words.remove(i);
                Ok(Some(v))
            }
            Some(_) => Err(format!("{name} needs a value")),
        }
    }

    fn parsed<T: std::str::FromStr>(&mut self, name: &str, default: T) -> Result<T, String> {
        match self.option(name)? {
            None => Ok(default),
            Some(v) => v.parse().map_err(|_| format!("bad value for {name}: {v}")),
        }
    }

    fn positional(&self, i: usize) -> Option<&str> {
        self.words.get(i).map(String::as_str)
    }
}

struct Session {
    socket: UdpSocket,
    payload: SocketAddr,
    timeout: Duration,
    cmd: Commander,
}

enum Outcome {
    Done,
    Rejected,
    Timeout,
}

impl Session {
    fn open(payload: &str, tm_port: u16, timeout: Duration) -> Result<Self, String> {
        let target = if payload.contains(':') {
            payload.to_string()
        } else {
            format!("{payload}:{}", mission::TC_PORT)
        };
        let payload = target
            .to_socket_addrs()
            .map_err(|e| format!("{target}: {e}"))?
            .find(SocketAddr::is_ipv4)
            .ok_or_else(|| format!("{target}: no IPv4 address"))?;
        let socket = UdpSocket::bind(("0.0.0.0", tm_port))
            .map_err(|e| format!("bind UDP {tm_port}: {e}"))?;
        Ok(Self {
            socket,
            payload,
            timeout,
            cmd: Commander::starting_at(
                std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .map_or(0, |d| (d.as_millis() & 0x3FFF) as u16),
            ),
        })
    }

    fn receive(&self, until: Instant) -> Option<Report> {
        let mut buf = [0u8; 2048];
        loop {
            let left = until.checked_duration_since(Instant::now())?;
            if left.is_zero() {
                return None;
            }
            self.socket.set_read_timeout(Some(left)).ok()?;
            match self.socket.recv_from(&mut buf) {
                Ok((n, _)) => {
                    if let Ok(tm) = decode_tm(&buf[..n]) {
                        return Some(interpret(&tm));
                    }
                }
                Err(e)
                    if e.kind() == std::io::ErrorKind::WouldBlock
                        || e.kind() == std::io::ErrorKind::TimedOut =>
                {
                    return None
                }
                Err(_) => return None,
            }
        }
    }

    /// Sends a telecommand and waits for its completion (and, for ST[17], the answer).
    fn execute(&mut self, tc: Vec<u8>, wants_pong: bool) -> Outcome {
        let seq = self.cmd.next_sequence().wrapping_sub(1) & 0x3FFF;
        if let Err(e) = self.socket.send_to(&tc, self.payload) {
            eprintln!("send: {e}");
            return Outcome::Rejected;
        }
        let deadline = Instant::now() + self.timeout;
        let mut completed = false;
        let mut pong = !wants_pong;
        while let Some(report) = self.receive(deadline) {
            match &report {
                Report::Verification {
                    sequence_count,
                    subtype,
                    failure,
                } if *sequence_count == seq => {
                    say(&report.to_string());
                    if failure.is_some() {
                        return Outcome::Rejected;
                    }
                    completed |= *subtype == 7;
                }
                Report::Pong => {
                    say(&report.to_string());
                    pong = true;
                }
                _ => {}
            }
            if completed && pong {
                return Outcome::Done;
            }
        }
        Outcome::Timeout
    }
}

fn run(mut args: Args) -> Result<ExitCode, String> {
    let payload = args
        .option("--payload")?
        .unwrap_or_else(|| "127.0.0.1".into());
    let tm_port: u16 = args.parsed("--tm-port", mission::TM_PORT)?;
    let timeout: f64 = args.parsed("--timeout", 10.0)?;
    let command = args.positional(0).ok_or(USAGE)?.to_string();
    if command == "help" || command == "--help" || command == "-h" {
        println!("{USAGE}");
        return Ok(ExitCode::SUCCESS);
    }
    let mut s = Session::open(&payload, tm_port, Duration::from_secs_f64(timeout))?;

    let tc = match command.as_str() {
        "ping" => {
            let tc = s.cmd.ping();
            return Ok(finish(s.execute(tc, true)));
        }
        "modcod" => {
            let m: u8 = args
                .positional(1)
                .and_then(|v| v.parse().ok())
                .filter(|m| *m < 5)
                .ok_or("modcod needs a value 0..4")?;
            s.cmd.set_modcod(m)
        }
        "acm" => {
            let on = match args.positional(1) {
                Some("on") => true,
                Some("off") => false,
                _ => return Err("acm on|off".into()),
            };
            let min: u8 = args.parsed("--min", 0)?;
            let max: u8 = args.parsed("--max", 4)?;
            let margin: f64 = args.parsed("--margin", 1.0)?;
            let hyst: f64 = args.parsed("--hyst", 1.0)?;
            s.cmd.set_acm(on, min, max, margin, hyst)
        }
        "channel" => match args.positional(1) {
            Some("clear") => s.cmd.set_channel(0, 0x7FFF),
            Some(v) => {
                let db: f64 = v.parse().map_err(|_| format!("bad Es/N0: {v}"))?;
                s.cmd.set_channel(mission::noise_level_for(db), 0x7FFF)
            }
            None => return Err("channel <Es/N0 dB> | clear".into()),
        },
        "pass" => match args.positional(1) {
            Some("start") => {
                let el: f64 = args.parsed("--el", 60.0)?;
                let zenith: f64 = args.parsed("--zenith", 20.0)?;
                let scale: u8 = args.parsed("--scale", 10)?;
                s.cmd.start_pass(el, zenith, scale)
            }
            Some("stop") => s.cmd.stop_pass(),
            _ => return Err("pass start|stop".into()),
        },
        "loopback" => {
            let mode = match args.positional(1) {
                Some("analog") => 0,
                Some("digital") => 1,
                Some("software") => 2,
                _ => return Err("loopback analog|digital|software".into()),
            };
            s.cmd.set_loopback(mode)
        }
        "restart" => s.cmd.restart_modem(),
        "hk" => {
            let count: usize = args.parsed("--count", 3)?;
            let sid: u8 = args.parsed("--sid", 0)?;
            let tc = s.cmd.ping(); // makes this port the telemetry destination
            s.socket
                .send_to(&tc, s.payload)
                .map_err(|e| e.to_string())?;
            let deadline = Instant::now() + s.timeout * u32::try_from(count).unwrap_or(1);
            let mut seen = 0;
            while seen < count {
                let Some(r) = s.receive(deadline) else {
                    return Ok(finish(Outcome::Timeout));
                };
                let matches = matches!(
                    (&r, sid),
                    (Report::Modem(_), 0 | 1)
                        | (Report::Link(_), 0 | 2)
                        | (Report::Platform(_), 0 | 3)
                        | (Report::Constellation(_), 0 | 4)
                );
                if matches {
                    if !say(&r.to_string()) {
                        break;
                    }
                    seen += 1;
                }
            }
            return Ok(ExitCode::SUCCESS);
        }
        "monitor" => {
            let seconds: f64 = args.parsed("--seconds", 30.0)?;
            let tc = s.cmd.ping();
            s.socket
                .send_to(&tc, s.payload)
                .map_err(|e| e.to_string())?;
            let deadline = Instant::now() + Duration::from_secs_f64(seconds);
            let start = Instant::now();
            while Instant::now() < deadline {
                if let Some(r) = s.receive(deadline) {
                    if !say(&format!("{:8.3}  {r}", start.elapsed().as_secs_f64())) {
                        break;
                    }
                }
            }
            return Ok(ExitCode::SUCCESS);
        }
        other => return Err(format!("unknown command {other}\n{USAGE}")),
    };
    Ok(finish(s.execute(tc, false)))
}

/// Prints a line; false once stdout is closed (e.g. piped into `head`).
fn say(line: &str) -> bool {
    use std::io::Write;
    writeln!(std::io::stdout().lock(), "{line}").is_ok()
}

fn finish(outcome: Outcome) -> ExitCode {
    match outcome {
        Outcome::Done => ExitCode::SUCCESS,
        Outcome::Rejected => ExitCode::from(1),
        Outcome::Timeout => {
            eprintln!("satlink: no answer from the payload");
            ExitCode::from(2)
        }
    }
}

fn main() -> ExitCode {
    let args = Args {
        words: std::env::args().skip(1).collect(),
    };
    match run(args) {
        Ok(code) => code,
        Err(e) => {
            eprintln!("satlink: {e}");
            ExitCode::from(64)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn args(v: &[&str]) -> Args {
        Args {
            words: v.iter().map(|s| (*s).to_string()).collect(),
        }
    }

    #[test]
    fn options_are_removed_from_the_positionals() {
        let mut a = args(&["pass", "--el", "45", "start", "--scale", "20"]);
        assert_eq!(a.parsed("--el", 60.0).unwrap(), 45.0);
        assert_eq!(a.parsed::<u8>("--scale", 10).unwrap(), 20);
        assert_eq!(a.parsed("--zenith", 20.0).unwrap(), 20.0);
        assert_eq!(a.positional(0), Some("pass"));
        assert_eq!(a.positional(1), Some("start"));
        assert!(args(&["x", "--el"]).option("--el").is_err());
        assert!(args(&["--el", "high"]).parsed("--el", 1.0).is_err());
    }
}
