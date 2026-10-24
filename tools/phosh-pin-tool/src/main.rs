// SPDX-FileCopyrightText: 2026 Phosh.mobi e.V.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Manages the `mobi.phosh.unlockInfo` object in a homed record.
//
// privilaged {
//    …,
//    mobi.phosh.unlockInfo {
//       pinLength: 6
//    }
// }

use clap::{Parser, Subcommand};
use serde_json::{Map, Value, json};
use std::time::{SystemTime, UNIX_EPOCH};
use std::{env, error::Error, process::exit};
use zbus::{Connection, OwnedObjectPath, proxy};

#[derive(Parser, Debug)]
#[command(
    name = "phosh-pin-tool",
    about = "Manage unlock settings",
    after_help = "You need to set the user password via the PASSWORD env var\nExamples:\n phosh-pin-tool set-pin-length --user foo 5"
)]
struct Cli {
    #[command(subcommand)]
    command: Command,
}

#[derive(Subcommand, Debug)]
enum Command {
    #[command(about = "Set the unlock PIN length")]
    SetPinLength {
        #[arg(long, help = "homed user to modify")]
        user: String,

        #[arg(long, help = "Length of the unlock PIN")]
        pin_length: Option<u32>,
    },

    #[command(about = "Get the unlock PIN length")]
    GetPinLength {
        #[arg(long, help = "homed user to get pin-length for")]
        user: String,
    },
}

#[proxy(
    interface = "org.freedesktop.home1.Manager",
    default_service = "org.freedesktop.home1",
    default_path = "/org/freedesktop/home1"
)]

trait HomeManager {
    #[zbus(allow_interactive_auth)]
    async fn update_home(&self, user_record: &str) -> zbus::Result<()>;
    #[zbus(allow_interactive_auth)]
    async fn get_user_record_by_name(
        &self,
        name: &str,
    ) -> zbus::Result<(String, bool, OwnedObjectPath)>;
}

async fn update_record(proxy: &HomeManagerProxy<'_>, mut rec: Value) -> Result<(), Box<dyn Error>> {
    let now = if let Ok(now) = SystemTime::now().duration_since(UNIX_EPOCH) {
        now.as_micros()
    } else {
        return Err("Failed to get system time".into());
    };
    rec["lastChangeUSec"] = Value::Number((now as u64).into());

    proxy.update_home(&(rec.to_string())).await?;

    Ok(())
}

async fn fetch_record(proxy: &HomeManagerProxy<'_>, user: &str) -> Result<Value, Box<dyn Error>> {
    let (record, incomplete, _path) = proxy.get_user_record_by_name(&user).await?;
    if incomplete {
        return Err("record is incomplete (no access to the privileged section);".into());
    }
    let mut rec: Value = serde_json::from_str(&record)?;

    let obj = rec.as_object_mut().ok_or("record is not a JSON object")?;
    // Sections homed regenerates, homectl does that too
    for k in ["binding", "status", "signature"] {
        obj.remove(k);
    }
    Ok(rec)
}

async fn set_unlock_info(
    user: &str,
    key: &str,
    value: Value,
    password: &str,
) -> Result<(), Box<dyn Error>> {
    let conn = zbus::Connection::system().await?;
    let proxy = HomeManagerProxy::new(&conn).await?;

    let mut rec: Value = fetch_record(&proxy, user).await?;
    rec["secret"] = json!({ "password": [password] });
    let privileged = rec
        .as_object_mut()
        .ok_or("record is not a JSON object")?
        .entry("privileged")
        .or_insert_with(|| Value::Object(Map::new()))
        .as_object_mut()
        .ok_or("'privileged' section is not an object")?;

    let unlock_info = privileged
        .entry("mobi.phosh.unlockInfo")
        .or_insert_with(|| Value::Object(Map::new()))
        .as_object_mut()
        .ok_or("'unlock_info' section is not an object")?;

    unlock_info.insert(key.into(), value);
    update_record(&proxy, rec).await?;
    Ok(())
}

async fn get_unlock_info(user: &str, key: &str) -> Result<Value, Box<dyn Error>> {
    let conn = Connection::system().await?;
    let proxy = HomeManagerProxy::new(&conn).await?;

    let mut rec: Value = fetch_record(&proxy, user).await?;
    let privileged = rec
        .as_object_mut()
        .ok_or("record is not a JSON object")?
        .entry("privileged")
        .or_insert_with(|| Value::Object(Map::new()))
        .as_object_mut()
        .ok_or("'privileged' section is not an object")?;

    let unlock_info = privileged
        .entry("mobi.phosh.unlockInfo")
        .or_insert_with(|| Value::Object(Map::new()))
        .as_object_mut()
        .ok_or("'unlock_info' section is not an object")?;

    unlock_info
        .get(key)
        .cloned()
        .ok_or_else(|| format!("{key} not present").into())
}

fn read_password() -> Result<String, Box<dyn Error>> {
    match env::var("PASSWORD") {
        Ok(pw) => Ok(pw),
        Err(env::VarError::NotPresent) => Ok(rpassword::prompt_password("Current PIN/Password: ")?),
        Err(e) => Err(e.into()),
    }
}

fn get_password() -> String {
    match read_password() {
        Ok(pw) => return pw,
        Err(e) => {
            eprintln!("Can't read password: {e}");
            exit(1);
        }
    };
}

fn main() -> Result<(), Box<dyn Error>> {
    let cli = Cli::parse();

    zbus::block_on(async {
        match cli.command {
            Command::SetPinLength { pin_length, user } => {
                let pw = get_password();
                let pin_length = pin_length.unwrap_or(pw.len() as u32);

                if pw.len() != pin_length as usize {
                    println!(
                        "Warning: PASSWORD length {} doesn't match pin-length {}",
                        pw.len(),
                        pin_length
                    );
                }
                let len = Value::Number(pin_length.into());
                if let Err(e) = set_unlock_info(&user, "pinLength", len, &pw).await {
                    eprintln!("Can't set pin-length: {e}");
                    exit(1);
                }
            }
            Command::GetPinLength { user } => {
                let value = get_unlock_info(&user, "pinLength")
                    .await
                    .unwrap_or_else(|e| {
                        eprintln!("Can't get pin-length: {e}");
                        exit(1);
                    });
                println!("pin-length: {}", value);
            }
        }
    });

    Ok(())
}
