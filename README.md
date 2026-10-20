# asmr-declick

A lightweight audio restoration tool for removing short transient artifacts from detailed recordings.

`asmr-declick` detects and repairs clicks, pops, and pulse-like noises while preserving the original character of the recording as much as possible.

Designed especially for close ASMR recordings, where small artifacts such as mouth clicks, kisses, and microphone noises can become highly noticeable.

## Features

* Detects short-duration transient artifacts
* Repairs clicks and pops with minimal impact on surrounding audio
* Supports PCM and multi-channel audio
* Supports WAV and Opus workflows
* Native C++ implementation
* Designed for batch processing and automation

## Motivation

Detailed recordings often contain localized transient noises:

* mouth clicks
* microphone pops
* digital glitches
* editing artifacts
* short impulse noises

General-purpose noise reduction can modify large portions of the signal. `asmr-declick` focuses on localized transient repair, targeting only detected artifacts while preserving surrounding audio.

## Samples

Short before/after comparison clips are provided for demonstration:

```text
samples/
├── original.opus
└── processed.opus
```

Source material:

* [Example ASMR recording](https://www.youtube.com/watch?v=gpjLCVEPcPw)

The original recording belongs to its respective creator. Samples are provided only for demonstrating the processing result.

## Build

### Requirements

* C++20 compiler
* CMake
* Ninja (recommended)

Clone:

```bash
git clone --recursive https://github.com/leaked32/asmr-declick.git
cd asmr-declick
```

Debug:

```bash
cmake --preset linux-x64-debug
cmake --build build/linux-x64-debug
```

Release:

```bash
cmake --preset linux-x64-release
cmake --build build/linux-x64-release
```

## Usage

Example:

```bash
asmr-declick --preset aggressive input.wav output.wav
```

Format conversion is supported:

```bash
asmr-declick --preset aggressive input.wav output.opus
asmr-declick --preset aggressive input.opus output.wav
```

Note: Opus is a lossy codec. Avoid repeated encoding and decoding cycles to prevent quality loss.

## Supported Audio

Currently supported:

* PCM audio
* Multi-channel recordings
* WAV input/output
* Opus input/output

More formats may be added in the future.

## License

MIT
