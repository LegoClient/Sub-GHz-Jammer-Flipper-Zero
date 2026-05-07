# Jams V1 - Flipper Zero RF Jammer

A polished, feature-rich sub-GHz jammer for the Flipper Zero, with the simple layout and run/pause/stop controls.

Built and tested against **Momentum firmware** (`mntm-011`, target `f7`, API `86.0`).

---

## ✨ Features

- **15 jamming modes** - OOK, 2FSK (low/high deviation), MSK, GFSK, Bruteforce, Sine, Square, Sawtooth, White Noise, Triangle, Chirp, Gaussian Noise, Burst, and Raw Noise (hardware RNG)
- **Run / Pause / Stop** control - short OK to toggle pause, long OK to fully stop
- **Sweep / Oscillate mode** - bounce between two frequencies with configurable step and speed
- **Digit-by-digit frequency tuning** across the full Flipper sub-GHz range (300 – 928 MHz)
- **Region bypass** - auto-installs a fake `FTW` country code so all bands are unlocked
- **Saveable presets** - 6 defaults plus on-the-fly save/load/delete (up to 20 total)
- **In-app menu** - clean submenus for mode select, presets, sweep, and a Raw Noise shortcut
- **Animated header** - antenna dots scan outward when running, retreat when paused, idle when stopped
- **RGB LED status indicator** - see at a glance what the app is doing
- **External / internal CC1101 auto-detect** - uses an external module if connected, otherwise the built-in radio

---

## 📡 Frequency Control

The app supports the three sub-GHz bands the Flipper hardware can drive:
- **Band 1**: 300 MHz – 348 MHz
- **Band 2**: 387 MHz – 464 MHz
- **Band 3**: 779 MHz – 928 MHz

On the main screen:
- **Left / Right** moves the digit cursor
- **Up / Down** increments / decrements the selected digit (hold to repeat)
- The frequency snaps to the nearest valid band edge if you cross into a forbidden gap

---

## 🎮 Controls

### Main (Jammer) Screen
| Key | Short | Long |
|---|---|---|
| OK | Start / Pause / Resume | Stop |
| Back | Open menu | Exit app |
| Up / Down | Adjust selected digit | - |
| Left / Right | Move digit cursor | - |

### Menu
- **Jamming Mode** - pick from the 15-mode list
- **Presets** - load, save, or delete frequency+mode combinations
- **Sweep** - configure and start an oscillating sweep
- **Raw Noise** - one-tap shortcut to the hardware-RNG noise mode
- **Back** - return to the jammer screen

---

## 💾 Presets

Loaded with 6 sensible defaults out of the box:

| Name | Frequency | Mode |
|---|---|---|
| Car Keys | 434.00 MHz | OOK 650 kHz |
| Car Keys US | 315.00 MHz | OOK 650 kHz |
| Garage | 390.00 MHz | OOK 650 kHz |
| Gate | 433.92 MHz | OOK 650 kHz |
| ISM 868 | 868.35 MHz | OOK 650 kHz |
| ISM 915 | 915.00 MHz | OOK 650 kHz |

In the Presets menu:
- **OK** on a preset → load it (frequency + mode), restart TX if running
- **OK** on `+ Save Current` → pick a name from the 17-name list to save the current frequency/mode
- **Long OK** on a preset → delete it
- **Back** → return to menu

Capacity is 20 presets total.

---

## 🔁 Sweep / Oscillate Mode

Hop the carrier back and forth between two frequencies - useful for covering narrow gaps where a target listens on more than one channel (e.g. 433.92 ↔ 434.00 MHz).

**Configuration screen:**
- **From** - locked to whatever frequency you were on when you entered Sweep
- **To** - adjustable: Up/Down ±10 kHz, Left/Right ±100 kHz
- **Step** - 10 kHz / 50 kHz / 100 kHz / 500 kHz
- **Speed** - Fast (50 ms) / Medium (150 ms) / Slow (500 ms)

**OK** advances row by row; on the Speed row it starts the sweep and returns to the main screen with the status badge showing `~~ SWEEPING ~~`. Switching jamming mode or stopping (long OK) clears sweep mode.

---

## 💡 LED Status Indicator

The Flipper's RGB LED reflects the jammer state at a glance:

| State | Colour | Pattern |
|---|---|---|
| Stopped | Red | Solid |
| Paused | Orange | Slow blink (~2 Hz) |
| Running | Green | Fast blink (~10 Hz, NFC-scan style) |
| Sweeping | Blue | Fast blink (~10 Hz) |

The LED is cleared on app exit.

---

## ⚙️ Jamming Modes Breakdown

Each jamming mode is implemented as a distinct modulation scheme and data pattern. The app generates these patterns and transmits them over the RF link to disrupt legitimate signals in the selected frequency range.

### 🦾 **OOK 650 kHz** (On-Off Keying)
- **Pattern**: Continuous stream of `0xFF` (carrier always on).
- **Mechanism**: OOK encodes data via the presence/absence of a carrier. With every byte all-ones, the carrier never drops.
- **Impact**: Overwhelms OOK receivers (garage doors, simple remotes, etc.) by occupying the channel continuously.

### ⚡ **2FSK 2.38 kHz** (Frequency Shift Keying)
- **Pattern**: Alternating `0xAA` / `0x55`, simulating a fast `01010101…` bitstream.
- **Mechanism**: 2FSK shifts between two close-set frequencies. A 2.38 kHz deviation is narrow and precise.
- **Impact**: Confuses narrowband receivers that expect a stable frequency-shifted bitstream.

### 🔥 **2FSK 47.6 kHz**
- **Pattern**: Same alternating `0xAA` / `0x55`.
- **Mechanism**: Wider 47.6 kHz deviation covers more of the channel.
- **Impact**: Effective against systems with wider channels or higher data rates.

### 💥 **MSK 99.97 Kb/s** (Minimum Shift Keying)
- **Pattern**: Pseudo-random bytes.
- **Mechanism**: MSK is spectrally efficient with continuous-phase shifts; random data simulates a high-speed link.
- **Impact**: Saturates receivers expecting MSK-style high-rate digital traffic.

### 📶 **GFSK 9.99 Kb/s** (Gaussian Frequency Shift Keying)
- **Pattern**: Pseudo-random bytes.
- **Mechanism**: GFSK applies a Gaussian filter to FSK transitions, common in Bluetooth and low-power RF.
- **Impact**: Disrupts low-power digital links by filling the channel with apparently legitimate GFSK traffic.

### 🚀 **Bruteforce 0xFF**
- **Pattern**: Constant `0xFF`.
- **Mechanism**: Pumps a flat all-ones bitstream - effectively a strong unmodulated carrier.
- **Impact**: The most aggressive jamming style; few receivers can demodulate around it.

### 🎶 **Sine Wave**
- **Pattern**: One full sine cycle per buffer, repeated.
- **Mechanism**: Smooth analog-like waveform.
- **Impact**: Disrupts analog modulation schemes by drowning them in a clean tone.

### 🟥 **Square Wave**
- **Pattern**: Alternating `0xFF` / `0x00`.
- **Mechanism**: Sharp digital pulses.
- **Impact**: Interferes with on-off / pulse-encoded signalling.

### 📈 **Sawtooth Wave**
- **Pattern**: Linear ramp 0 → 255, repeated.
- **Mechanism**: Resembles a slow frequency sweep within each buffer.
- **Impact**: Effective against frequency-sensitive systems lacking error correction.

### 🎲 **White Noise**
- **Pattern**: Pseudo-random bytes from `rand()`.
- **Mechanism**: Spread-spectrum-style flat noise.
- **Impact**: Universal disruption - works against most modulations.

### 🔺 **Triangle Wave**
- **Pattern**: Linear ramp up then down.
- **Mechanism**: Symmetric oscillation.
- **Impact**: Disrupts predictably-modulated systems.

### 📡 **Chirp Signal**
- **Pattern**: Sine with rising instantaneous frequency.
- **Mechanism**: Radar/sonar-style frequency sweep.
- **Impact**: Smears across a band, confusing demodulators that expect a stable carrier.

### 🎲 **Gaussian Noise**
- **Pattern**: Bytes drawn from a Box-Muller normal distribution.
- **Mechanism**: Statistically natural-looking noise.
- **Impact**: Especially good against Gaussian-modulated digital links - looks like ambient RF noise.

### 💥 **Burst Mode**
- **Pattern**: Single `0xFF` byte every 10 bytes, the rest zero.
- **Mechanism**: Periodic high-energy pulses.
- **Impact**: Mimics packet-based traffic, confusing burst-receiver state machines.

### 🌪 **Raw Noise**
- **Pattern**: Bytes from `furi_hal_random_fill_buf()` - the Flipper's hardware RNG.
- **Mechanism**: True hardware-entropy noise, regenerated per buffer.
- **Impact**: A higher-quality random source than the C `rand()` PRNG; useful when you want noise that won't repeat predictably.

---

## 🛠 Build

The project is set up for [`ufbt`](https://github.com/flipperdevices/flipperzero-ufbt) against the Momentum firmware SDK.

```bash
# one-time SDK install (Momentum mntm-011 / target f7)
ufbt update --hw-target=f7 --url=https://github.com/Next-Flip/Momentum-Firmware/releases/download/mntm-011/flipper-z-f7-sdk-mntm-011.zip

# build
ufbt
```

The compiled FAP lands in `dist/jammer_app.fap`. Drop it onto your Flipper's SD card under `/ext/apps/Sub-GHz/` (or any folder you like) and launch from the apps browser.

---

## ⚠️ Legal

Transmitting on these frequencies without authorisation is illegal in most jurisdictions. This tool is for educational research, RF testing on equipment you own, and CTF / authorised pentesting only. **You are responsible for how you use it.**

---

## 🙏 Credits

- Original wide-mode jammer: [`flipper-zero-rf-jammer`](https://github.com/RocketGod-Git/Flipper-Zero-Jammer) by `@RocketGod-git`
- Pause/run UX inspiration: `jamRF`
- Combined, polished, and extended into this app.
