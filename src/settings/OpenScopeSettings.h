#pragma once

struct OpenScopeSettings
{
    enum class AspectRatio
    {
        Ratio4x3,
        Ratio16x9
    };

    static constexpr double aspectRatioValue(
        AspectRatio aspectRatio) noexcept
    {
        return
            aspectRatio == AspectRatio::Ratio4x3
            ? 4.0 / 3.0
            : 16.0 / 9.0;
    }

    enum class WorkspaceView
    {
        Headless = -1,
        Matrix = 0,
        Video = 1,
        Waveform = 2,
        Vectorscope = 3
    };

    struct Control
    {
        struct Instrument
        {
            int lineNumber = 320;

            struct Waveform
            {
                int zoom = 1;
                double scrollPosition = 0.0;

                bool vintageLook = true;
                int chromaRenderIntensity = 150;

                int persistenceFrames = 5;
            };

            struct Vectorscope
            {
                bool showHundredPercentTargets = true;

                int persistenceFrames = 5;
                int glow = 50;
            };

            Waveform waveform;
            Vectorscope vectorscope;
        };

        struct Processing
        {
            struct NoiseFilter
            {
                bool enabled = false;

                int strength = 50;
                int temporalStrength = 0;
            };

            NoiseFilter noiseFilter;
        };

        struct VideoOut
        {
            bool enabled = false;

            int width = 1920;
            int height = 1080;

            AspectRatio aspectRatio =
                AspectRatio::Ratio16x9;

            double underscan = 0.80;

            bool deinterlace = false;
        };

        Instrument instrument;
        Processing processing;
        VideoOut videoOut;
    };

    struct Local
    {
        struct Display
        {
            double gamma = 0.80;

            AspectRatio aspectRatio =
                AspectRatio::Ratio16x9;

            bool deinterlace = false;
        };

        struct Window
        {
            int x = 100;
            int y = 100;

            int width = 1280;
            int height = 720;

            bool maximized = false;
        };

        struct Workspace
        {
            WorkspaceView view =
                WorkspaceView::Matrix;
        };

        struct Floaty
        {
            int x = 0;
            int y = 0;

            bool positionValid = false;
        };

        struct Floaties
        {
            Floaty performance;
            Floaty settings;

            bool performanceVisible = true;
            bool waveformVideoVisible = false;
        };

        Display display;
        Window window;
        Workspace workspace;
        Floaties floaties;
    };

    Control control;
    Local local;
};