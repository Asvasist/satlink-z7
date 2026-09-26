//! The `satlink` binary against a fake payload on localhost: telecommands go out as valid PUS
//! packets, verification reports decide the exit status.
//!
//! @verifies SRS-GS-003

use satlink_cli::pus::{encode_tm, Telemetry};
use std::net::UdpSocket;
use std::process::Command;
use std::thread;
use std::time::Duration;

fn report(service: u8, subtype: u8, data: Vec<u8>) -> Vec<u8> {
    encode_tm(&Telemetry {
        apid: 0x010,
        sequence_count: 0,
        service,
        subtype,
        message_counter: 0,
        destination_id: 1,
        seconds: 0,
        fraction: 0,
        data,
    })
}

/// Answers one telecommand like the payload: acceptance, then completion or failure.
fn fake_payload(fail_with: Option<u16>) -> (u16, thread::JoinHandle<Vec<u8>>) {
    let sock = UdpSocket::bind("127.0.0.1:0").unwrap();
    let port = sock.local_addr().unwrap().port();
    let handle = thread::spawn(move || {
        sock.set_read_timeout(Some(Duration::from_secs(10)))
            .unwrap();
        let mut buf = [0u8; 512];
        let (n, from) = sock.recv_from(&mut buf).unwrap();
        let tc = buf[..n].to_vec();
        let id = [tc[0] | 0x10, tc[1], tc[2], tc[3]].to_vec();
        sock.send_to(&report(1, 1, id.clone()), from).unwrap();
        if tc[7] == 17 {
            sock.send_to(&report(17, 2, vec![]), from).unwrap();
        }
        let done = match fail_with {
            None => report(1, 7, id),
            Some(code) => {
                let mut d = id;
                d.extend_from_slice(&code.to_be_bytes());
                report(1, 8, d)
            }
        };
        sock.send_to(&done, from).unwrap();
        tc
    });
    (port, handle)
}

/// Runs the CLI with an OS-assigned telemetry port (the fake payload answers the sender).
fn satlink(tc_port: u16, args: &[&str]) -> std::process::Output {
    Command::new(env!("CARGO_BIN_EXE_satlink"))
        .args([
            "--payload",
            &format!("127.0.0.1:{tc_port}"),
            "--tm-port",
            "0",
            "--timeout",
            "5",
        ])
        .args(args)
        .output()
        .unwrap()
}

#[test]
fn ping_succeeds() {
    let (port, payload) = fake_payload(None);
    let out = satlink(port, &["ping"]);
    let tc = payload.join().unwrap();
    assert!(out.status.success(), "{out:?}");
    assert_eq!((tc[7], tc[8]), (17, 1));
    let text = String::from_utf8_lossy(&out.stdout);
    assert!(
        text.contains("acceptance OK") && text.contains("alive") && text.contains("completion OK")
    );
}

#[test]
fn pass_command_carries_its_arguments() {
    let (port, payload) = fake_payload(None);
    let out = satlink(
        port,
        &[
            "pass", "start", "--el", "80", "--zenith", "20", "--scale", "25",
        ],
    );
    let tc = payload.join().unwrap();
    assert!(out.status.success(), "{out:?}");
    assert_eq!(&tc[7..17], &[8, 1, 0, 1, 4, 0x1F, 0x40, 0x07, 0xD0, 25]);
}

#[test]
fn rejection_gives_exit_status_1() {
    let (port, payload) = fake_payload(Some(5));
    let out = satlink(port, &["restart"]);
    payload.join().unwrap();
    assert_eq!(out.status.code(), Some(1));
    assert!(String::from_utf8_lossy(&out.stdout).contains("FAILED (modem error)"));
}

#[test]
fn silence_gives_exit_status_2() {
    let quiet = UdpSocket::bind("127.0.0.1:0").unwrap();
    let port = quiet.local_addr().unwrap().port();
    let out = Command::new(env!("CARGO_BIN_EXE_satlink"))
        .args(["--payload", &format!("127.0.0.1:{port}"), "--tm-port", "0"])
        .args(["--timeout", "0.3", "channel", "clear"])
        .output()
        .unwrap();
    assert_eq!(out.status.code(), Some(2));
}

#[test]
fn usage_errors_give_exit_status_64() {
    let port = 9; // discard: nothing is sent for usage errors
    for args in [
        &["modcod", "9"][..],
        &["acm", "maybe"],
        &["frobnicate"],
        &[],
    ] {
        let out = satlink(port, args);
        assert_eq!(out.status.code(), Some(64), "{args:?}");
    }
    let out = satlink(port, &["help"]);
    assert!(out.status.success());
    assert!(String::from_utf8_lossy(&out.stdout).contains("usage: satlink"));
}
