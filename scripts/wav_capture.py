#!/usr/bin/env python3
"""
Reads pocket_sim's raw PCM stream directly off a UART device (or a
native_sim pseudo-tty) and plays each rendered sequence back as soon
as it's fully received, saving a .wav alongside it.

Stream format (written by src/main.c), sent once per "seq render":
    4 bytes  magic "PSIM"
    4 bytes  little-endian uint32 sample rate
    N bytes  little-endian int16 PCM samples, mono

There's no explicit end-of-sequence marker in the stream, so this
script treats a stretch of silence on the line (no bytes for
IDLE_TIMEOUT seconds) as "the render just finished" and plays back /
saves whatever it collected since the last PSIM header. If a new
PSIM header shows up before that timeout (e.g. you ran `seq render`
again quickly), it finalizes the previous sequence first.

Usage:
    # live monitor, plays each sequence as it completes
    python3 wav_capture.py /dev/pts/7
    python3 wav_capture.py /dev/ttyUSB0 --baud 115200
    python3 wav_capture.py /dev/pts/7 --no-play      # save only

    # one-shot: convert (and play) a file you already captured
    python3 wav_capture.py --from-file raw.bin out.wav

Playback tries, in order: the `simpleaudio` package, then a platform
command-line player (afplay / paplay / aplay / PowerShell). If none
of those are available the .wav is still written -- open it by hand.

For a real UART (not a native_sim pty), install pyserial for proper
baud-rate handling: `pip install pyserial`. Without it, this falls
back to a raw non-blocking file read, which is fine for native_sim's
pty but won't configure baud rate on real hardware.
"""
import argparse
import os
import platform
import struct
import subprocess
import sys
import time
import wave

MAGIC = b"PSIM"
IDLE_TIMEOUT = 1.0   # seconds of silence => sequence finished
READ_CHUNK = 4096
OUT_DIR = "captures"


# --- device I/O --------------------------------------------------------

def open_device(port: str, baud: int):
    """Best effort: pyserial if available (proper baud handling for
    real UARTs), otherwise a raw non-blocking file read -- enough for
    native_sim's pseudo-tty.
    """
    try:
        import serial
        ser = serial.Serial(port, baudrate=baud, timeout=0.1)
        return ("pyserial", ser)
    except ImportError:
        print("(pyserial not installed -- falling back to raw file reads. "
              "`pip install pyserial` for real UART hardware.)")
    except Exception as e:
        print(f"pyserial couldn't open {port} ({e}), falling back to raw file reads.")

    fd = os.open(port, os.O_RDONLY | os.O_NONBLOCK)
    return ("raw", fd)


def read_some(handle) -> bytes:
    kind, obj = handle
    if kind == "pyserial":
        n = obj.in_waiting or 1
        return obj.read(n)
    try:
        return os.read(obj, READ_CHUNK)
    except BlockingIOError:
        return b""
    except OSError:
        return b""


def close_device(handle):
    kind, obj = handle
    if kind == "pyserial":
        obj.close()
    else:
        os.close(obj)


# --- playback ------------------------------------------------------------

def play_wav(path: str) -> bool:
    try:
        import simpleaudio as sa
        sa.WaveObject.from_wave_file(path).play().wait_done()
        return True
    except ImportError:
        pass
    except Exception as e:
        print(f"simpleaudio playback failed ({e}), trying a system player...")

    system = platform.system()
    try:
        if system == "Darwin":
            subprocess.run(["afplay", path], check=True)
            return True
        if system == "Linux":
            for player in ("paplay", "aplay"):
                try:
                    subprocess.run([player, path], check=True,
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                    return True
                except (FileNotFoundError, subprocess.CalledProcessError):
                    continue
            print("No paplay/aplay found -- `pip install simpleaudio` "
                  f"or play {path} manually.")
            return False
        if system == "Windows":
            import winsound
            winsound.PlaySound(path, winsound.SND_FILENAME)
            return True
    except Exception as e:
        print(f"Playback failed ({e}) -- the .wav is saved at {path}, play it manually.")
    print(f"Don't know how to auto-play on {system} -- open {path} manually.")
    return False


# --- wav writing -----------------------------------------------------

def save_wav(rate: int, pcm: bytes, path: str):
    pcm = pcm[: len(pcm) - (len(pcm) % 2)]  # trim to whole int16 samples
    with wave.open(path, "w") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm)
    return len(pcm) // 2


def finish_sequence(rate: int, pcm: bytes, index: int, do_play: bool):
    if not pcm:
        return
    os.makedirs(OUT_DIR, exist_ok=True)
    path = os.path.join(OUT_DIR, f"sequence_{index:03d}.wav")
    n_samples = save_wav(rate, pcm, path)
    print(f"Sequence complete: {n_samples} samples at {rate} Hz -> {path}")
    if do_play:
        print("Playing back...")
        play_wav(path)


# --- live monitor ------------------------------------------------------

def monitor(port: str, baud: int, do_play: bool):
    print(f"Opening {port} ...")
    handle = open_device(port, baud)
    print("Listening for sequences (Ctrl+C to stop)...")

    buf = b""
    rate = None
    collecting = False
    last_byte_time = time.time()
    seq_index = 1

    try:
        while True:
            chunk = read_some(handle)
            now = time.time()

            if chunk:
                buf += chunk
                last_byte_time = now

                if not collecting:
                    idx = buf.find(MAGIC)
                    if idx != -1 and len(buf) >= idx + 8:
                        rate = struct.unpack_from("<I", buf, idx + 4)[0]
                        buf = buf[idx + 8:]
                        collecting = True
                        print(f"Sequence started (rate={rate} Hz)...")
                    elif len(buf) > 4096:
                        # no header yet; don't let the buffer grow forever
                        buf = buf[-8:]
                else:
                    # a new PSIM mid-stream means a render was retriggered
                    # before the idle timeout -- finalize the old one first
                    idx = buf.find(MAGIC)
                    if idx != -1 and idx > 0:
                        finish_sequence(rate, buf[:idx], seq_index, do_play)
                        seq_index += 1
                        rate = struct.unpack_from("<I", buf, idx + 4)[0]
                        buf = buf[idx + 8:]
                        print(f"New sequence started (rate={rate} Hz)...")
            elif collecting and (now - last_byte_time) > IDLE_TIMEOUT:
                finish_sequence(rate, buf, seq_index, do_play)
                seq_index += 1
                buf = b""
                collecting = False
                rate = None
                print("Listening for sequences (Ctrl+C to stop)...")
            else:
                time.sleep(0.02)
    except KeyboardInterrupt:
        print("\nStopping.")
        if collecting and buf:
            finish_sequence(rate, buf, seq_index, do_play)
    finally:
        close_device(handle)


# --- one-shot file conversion (unchanged use case) ----------------------

def convert_file(in_path: str, out_path: str, do_play: bool):
    with open(in_path, "rb") as f:
        data = f.read()
    idx = data.find(MAGIC)
    if idx == -1:
        print("Didn't find the PSIM header in this capture -- make sure "
              "you captured from the start of a render.")
        sys.exit(1)
    rate = struct.unpack_from("<I", data, idx + 4)[0]
    pcm = data[idx + 8:]
    n_samples = save_wav(rate, pcm, out_path)
    print(f"Wrote {n_samples} samples at {rate} Hz -> {out_path}")
    if do_play:
        play_wav(out_path)


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument(
        "target",
        nargs="?",
        help="UART device to monitor live (e.g. /dev/pts/7, /dev/ttyUSB0), "
             "or the output .wav path when used with --from-file",
    )
    parser.add_argument("--baud", type=int, default=115200,
                         help="baud rate for real UARTs (ignored for ptys, default 115200)")
    parser.add_argument("--from-file", metavar="RAW_BIN",
                         help="convert/play a previously captured raw file instead of "
                              "monitoring a live device")
    parser.add_argument("--no-play", action="store_true",
                         help="save .wav files but don't auto-play them")
    args = parser.parse_args()

    do_play = not args.no_play

    if args.from_file:
        convert_file(args.from_file, args.target or "audio.wav", do_play)
        return

    if not args.target:
        parser.print_help()
        sys.exit(1)

    monitor(args.target, args.baud, do_play)


if __name__ == "__main__":
    main()
