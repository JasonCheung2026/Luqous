#!/usr/bin/env python3
"""Download sensor_log.csv from the ESP32 over MQTT and save it locally."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from pathlib import Path

import paho.mqtt.client as mqtt

DOWNLOAD_COMMAND = '{"cmd":"download_log"}'


class DownloadState:
    def __init__(self) -> None:
        self.started = False
        self.finished = False
        self.error: str | None = None
        self.lines: dict[int, str] = {}
        self.expected_lines: int | None = None


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Download sensor_log.csv from ESP32 via MQTT."
    )
    parser.add_argument(
        "--broker",
        default="15d8cf01fbae43a0a8806e281d097287.s1.eu.hivemq.cloud",
        help="MQTT broker hostname (default: HiveMQ Cloud cluster)",
    )
    parser.add_argument(
        "--port",
        type=int,
        default=8883,
        help="MQTT broker port (default: 8883 for HiveMQ TLS)",
    )
    parser.add_argument(
        "--tls",
        action="store_true",
        default=True,
        help="Use TLS (default: on for HiveMQ Cloud)",
    )
    parser.add_argument(
        "--no-tls",
        action="store_true",
        help="Disable TLS (local Mosquitto on port 1883)",
    )
    parser.add_argument(
        "--username",
        default="Luquos",
        help="MQTT username (HiveMQ Cloud credential)",
    )
    parser.add_argument(
        "--password",
        default="",
        help="MQTT password (HiveMQ Cloud credential)",
    )
    parser.add_argument(
        "--node",
        default="node1",
        help="PCB node/topic namespace (default: node1)",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("sensor_log.csv"),
        help="Output CSV file path (default: sensor_log.csv)",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=30.0,
        help="Seconds to wait for download to finish (default: 30)",
    )
    parser.add_argument(
        "--open",
        action="store_true",
        help="Open the saved CSV in the default app after download (Excel on Mac if configured)",
    )
    return parser.parse_args()


def create_client(
    state: DownloadState,
    username: str,
    password: str,
    topic_command: str,
    topic_download: str,
) -> mqtt.Client:
    try:
        client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION1)
    except AttributeError:
        client = mqtt.Client()

    if username:
        client.username_pw_set(username, password)

    def on_connect(client: mqtt.Client, userdata, flags, rc) -> None:
        if rc != 0:
            state.error = f"MQTT connect failed with code {rc}"
            return
        client.subscribe(TOPIC_DOWNLOAD)
        client.subscribe(topic_download)
        client.publish(topic_command, DOWNLOAD_COMMAND, qos=0)
        print(f"Requested log download on {topic_command}")

    def on_message(client: mqtt.Client, userdata, msg) -> None:
        try:
            payload = json.loads(msg.payload.decode("utf-8"))
        except json.JSONDecodeError as exc:
            state.error = f"Invalid JSON on {topic_download}: {exc}"
            return

        status = payload.get("status")
        if status == "start":
            state.started = True
            state.lines.clear()
            source = payload.get("data", "sensor_log.csv")
            print(f"Download started ({source})")
        elif status == "line":
            line_no = payload.get("line")
            data = payload.get("data")
            if isinstance(line_no, int) and isinstance(data, str):
                state.lines[line_no] = data
        elif status == "end":
            state.expected_lines = payload.get("lines")
            state.finished = True
            print(f"Download finished ({len(state.lines)} lines received)")
        elif status == "error":
            state.error = payload.get("data", "unknown error")
        else:
            state.error = f"Unexpected status: {status!r}"

    client.on_connect = on_connect
    client.on_message = on_message
    return client


def write_csv(output: Path, lines: dict[int, str]) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    ordered = [lines[key] for key in sorted(lines)]
    output.write_text("\n".join(ordered) + "\n", encoding="utf-8")


def maybe_open_file(path: Path) -> None:
    if sys.platform == "darwin":
        subprocess.run(["open", str(path)], check=False)
    elif sys.platform.startswith("linux"):
        subprocess.run(["xdg-open", str(path)], check=False)
    elif sys.platform == "win32":
        subprocess.run(["start", "", str(path)], shell=True, check=False)


def main() -> int:
    args = parse_args()
    use_tls = not args.no_tls
    if use_tls and not args.password:
        print(
            "Error: --password is required for HiveMQ Cloud (TLS).",
            file=sys.stderr,
        )
        return 1

    topic_command = f"esp32/{args.node}/commands"
    topic_download = f"esp32/{args.node}/log/download"

    state = DownloadState()
    client = create_client(
        state,
        args.username,
        args.password,
        topic_command=topic_command,
        topic_download=topic_download,
    )

    if use_tls:
        client.tls_set()  # system CA store
        print(f"Connecting to mqtts://{args.broker}:{args.port} (TLS) ...")
    else:
        print(f"Connecting to mqtt://{args.broker}:{args.port} ...")

    client.connect(args.broker, args.port, keepalive=60)
    client.loop_start()

    deadline = time.time() + args.timeout
    try:
        while time.time() < deadline:
            if state.error:
                print(f"Error: {state.error}", file=sys.stderr)
                return 1
            if state.finished:
                if not state.lines:
                    print("Error: download finished but no lines were received.", file=sys.stderr)
                    return 1

                if state.expected_lines is not None and len(state.lines) != state.expected_lines:
                    print(
                        f"Warning: expected {state.expected_lines} lines, got {len(state.lines)}",
                        file=sys.stderr,
                    )

                write_csv(args.output, state.lines)
                print(f"Saved {len(state.lines)} lines to {args.output.resolve()}")

                if args.open:
                    maybe_open_file(args.output.resolve())

                return 0

            time.sleep(0.1)

        if not state.started:
            print(
                "Timed out waiting for ESP32 response. "
                "Check hotspot, broker IP, and that the ESP32 is connected to MQTT.",
                file=sys.stderr,
            )
        else:
            print("Timed out before download finished.", file=sys.stderr)
        return 1
    finally:
        client.loop_stop()
        client.disconnect()


if __name__ == "__main__":
    raise SystemExit(main())
