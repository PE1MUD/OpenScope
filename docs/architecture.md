Capture
    ↓
Frame
    ↓
Processing
    ↓
Render
    ↓
Qt Widget


Ownership

DeckLink callback
        │
        ▼
Frame acquisition
        │
        ▼
Frame queue
        │
        ▼
Processing thread
        │
        ▼
GUI thread