#pragma once

struct OpenScopeSettings
{
    enum class AspectRatio
    {
        Ratio4x3,
        Ratio16x9
    };

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
                int zoomStartSample = 0;

                bool vintageLook = true;
                int chromaRenderIntensity = 150;

                int persistenceFrames = 5;
            };

            struct Vectorscope
            {
                bool showHundredPercentTargets = true;

                int persistenceFrames = 5;
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
                AspectRatio::Ratio4x3;

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

        Display display;
        Window window;
        Workspace workspace;
    };

    Control control;
    Local local;
};