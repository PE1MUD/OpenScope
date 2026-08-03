# OpenScope Coding Style

## General

- Keep code simple.
- Prefer readability over cleverness.
- Optimise only after measuring.

## Formatting

- Kernighan & Ritchie brace style.
- Four spaces indentation.
- No tabs.
- UTF-8 source files.

Example:

```cpp
if (ready) {
    processFrame();
} else {
    reset();
}
```

## Naming

Classes:

```cpp
WaveformWidget
DeckLinkCapture
```

Functions and variables:

```cpp
processFrame()
frameCount
```

Private members:

```cpp
frameCount_
captureDevice_
```

Constants:

```cpp
constexpr int MaxAudioChannels = 16;
```

## C++

- Use `nullptr`.
- Use `explicit` where appropriate.
- Prefer RAII.
- Avoid owning raw pointers.
- Use `const` whenever possible.
- Use `#pragma once`.

## Architecture

- One responsibility per class.
- No interfaces until multiple implementations exist.
- No premature abstractions.
- Composition over inheritance where practical.

## Comments

Explain **why**, not **what**.

## Git

Small commits.

Each commit should represent one logical change.