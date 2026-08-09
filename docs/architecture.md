DeckLink capture
       │
       ▼
   Yuv444Frame
       │
       ▼
SignalReconstructor
       │
       │  Fixed higher horizontal processing resolution (x3 or x4)
       │  with selectable threading model, depending on your cpu capabilities
       ▼
Reconstructed frame
       │
       ├──► Video display (4:3 or 16:9)
       ├──► Waveform analysis / rendering
       │        (with Catmull-Rom Spline, x1, x5, x10 and clever chroma rendering)
       └──► Vectorscope analysis / rendering (with Catmull-Rom Spline)
                    │
                    ▼
                 Qt Widgets