#include "PluginEditor.h"

using namespace juce;

static const char* kIds[15]    { "intensity", "density", "size", "pitch", "arp", "grain", "feedback", "position",
                                 "scan", "reverb", "delay", "crush", "filter", "drive", "out" };
static const char* kLabels[15] { "INTENSITY", "DENSITY", "SIZE", "PITCH", "ARP", "GRAIN", "FEEDBACK", "POSITION",
                                 "SCAN", "REVERB", "DELAY", "CRUSH", "FILTER", "DRIVE", "OUTPUT" };

static String fmtValue (int idx, double v)
{
    switch (idx)
    {
        case 3:   // pitch: 50% = sin cambio, +-12 semitonos
        {
            const double st = (v - 50.0) / 50.0 * 12.0;
            return String (v, 0) + "%  " + (st >= 0 ? "+" : "") + String (st, 1) + "st";
        }
        case 12:  // filtro bipolar
            if (v < -1.0) return "LP " + String (-v, 0) + "%";
            if (v > 1.0)  return "HP " + String (v, 0) + "%";
            return "OFF";
        case 14:  return String (v, 1) + " dB";
        default:  return String (v, 0) + "%";
    }
}

//==============================================================================
FragatimeAudioProcessorEditor::FragatimeAudioProcessorEditor (FragatimeAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&laf);
    srcP = proc.apvts.getRawParameterValue ("source");
    dirP = proc.apvts.getRawParameterValue ("dir");

    for (size_t i = 0; i < 15; ++i)
    {
        auto& s = knobs[i];
        s.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (Slider::TextBoxBelow, false, 92, 16);
        if (i == 3) s.getProperties().set ("center", true);
        addAndMakeVisible (s);
        atts[i] = std::make_unique<SliderAtt> (proc.apvts, kIds[i], s);

        const int idx = (int) i;
        s.textFromValueFunction = [idx] (double v) { return fmtValue (idx, v); };
        s.updateText();
    }

    loadBtn.setButtonText ("LOAD AUDIO");
    loadBtn.onClick = [this]
    {
        chooser = std::make_unique<FileChooser> ("Select an audio file (wav / mp3)", File(),
                                                 "*.wav;*.mp3;*.aif;*.aiff;*.flac;*.ogg");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                              [this] (const FileChooser& fc)
                              {
                                  const auto f = fc.getResult();
                                  if (f.existsAsFile()) proc.loadFile (f);
                              });
    };
    addAndMakeVisible (loadBtn);

    srcBtn.onClick = [this] { cycleChoice ("source", 2); };
    dirBtn.onClick = [this] { cycleChoice ("dir", 3); };
    addAndMakeVisible (srcBtn);
    addAndMakeVisible (dirBtn);

    themeBtn.onClick = [this]
    {
        proc.apvts.state.setProperty ("dark", ! isDark(), nullptr);
        applyTheme();
    };
    addAndMakeVisible (themeBtn);

    applyTheme();
    setSize (960, 590);
    startTimerHz (30);
}

FragatimeAudioProcessorEditor::~FragatimeAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

//==============================================================================
bool FragatimeAudioProcessorEditor::isDark() const
{
    return (bool) proc.apvts.state.getProperty ("dark", true);
}

FragatimeAudioProcessorEditor::Pal FragatimeAudioProcessorEditor::pal() const
{
    if (isDark()) return { Colour (0xff0c0c0c), Colours::white, Colour (0xff7a7a7a), Colour (0xff1b1b1b) };
    return { Colour (0xfff2f2f0), Colour (0xff111111), Colour (0xff808080), Colour (0xffe2e2df) };
}

void FragatimeAudioProcessorEditor::applyTheme()
{
    const auto p = pal();
    laf.fg = p.fg;
    laf.track = p.dim.withAlpha (0.35f);

    laf.setColour (Slider::textBoxTextColourId, p.fg);
    laf.setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    laf.setColour (ComboBox::outlineColourId, p.dim);
    laf.setColour (TextButton::buttonColourId, p.bg);
    laf.setColour (TextButton::buttonOnColourId, p.bg);
    laf.setColour (TextButton::textColourOffId, p.fg);
    laf.setColour (TextButton::textColourOnId, p.fg);
    laf.setColour (Label::textColourId, p.fg);

    themeBtn.setButtonText (isDark() ? "LIGHT" : "DARK");
    lastDark = isDark();
    sendLookAndFeelChange();
    repaint();
}

void FragatimeAudioProcessorEditor::setParam (const String& id, float v)
{
    if (auto* p = proc.apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (v));
        p->endChangeGesture();
    }
}

void FragatimeAudioProcessorEditor::cycleChoice (const char* id, int numItems)
{
    const int cur = (int) std::round (proc.apvts.getRawParameterValue (id)->load());
    setParam (id, (float) ((cur + 1) % numItems));
}

//==============================================================================
void FragatimeAudioProcessorEditor::resized()
{
    const int W = getWidth();
    loadBtn.setBounds (240, 14, 120, 28);
    srcBtn.setBounds (W - 24 - 80 - 8 - 130 - 8 - 130, 14, 130, 28);
    dirBtn.setBounds (W - 24 - 80 - 8 - 130, 14, 130, 28);
    themeBtn.setBounds (W - 24 - 80, 14, 80, 28);

    waveArea = { 24, 58, W - 48, 150 };

    knobs[0].setBounds (24, 246, 150, 150);                       // INTENSITY (grande)

    const float x0 = 196.f, slotW = ((float) W - 24.f - x0) / 8.f;
    for (int i = 1; i <= 8; ++i)                                   // fila GRANO
        knobs[(size_t) i].setBounds ((int) (x0 + slotW * (float) (i - 1)), 256, (int) slotW, 120);
    for (int i = 9; i <= 14; ++i)                                  // fila FX
        knobs[(size_t) i].setBounds ((int) (x0 + slotW * (float) (i - 9)), 432, (int) slotW, 120);
}

void FragatimeAudioProcessorEditor::timerCallback()
{
    if (isDark() != lastDark) applyTheme();

    const int v = proc.fileVersion.load();
    if (v != lastVersion) { lastVersion = v; proc.getWaveform (wave); }

    const int src = (int) std::round (srcP->load());
    const int dir = (int) std::round (dirP->load());
    static const char* dirNames[3] { "FWD", "REV", "RND" };
    srcBtn.setButtonText (String ("SOURCE: ") + (src == 0 ? "FILE" : "LIVE"));
    dirBtn.setButtonText (String ("DIRECTION: ") + dirNames[jlimit (0, 2, dir)]);
    repaint();
}

//==============================================================================
bool FragatimeAudioProcessorEditor::isInterestedInFileDrag (const StringArray& files)
{
    for (auto& f : files)
    {
        const auto e = File (f).getFileExtension().toLowerCase();
        if (e == ".wav" || e == ".mp3" || e == ".aif" || e == ".aiff" || e == ".flac" || e == ".ogg") return true;
    }
    return false;
}

void FragatimeAudioProcessorEditor::filesDropped (const StringArray& files, int, int)
{
    for (auto& f : files)
        if (File (f).existsAsFile()) { proc.loadFile (File (f)); break; }
}

void FragatimeAudioProcessorEditor::setPositionFromMouse (const MouseEvent& e)
{
    if (wave.empty() || (int) std::round (srcP->load()) != 0) return;
    const float t = jlimit (0.f, 1.f, (e.position.x - (float) waveArea.getX()) / (float) waveArea.getWidth());
    setParam ("position", t * 100.f);
}

void FragatimeAudioProcessorEditor::mouseDown (const MouseEvent& e)
{
    if (waveArea.contains (e.getPosition())) setPositionFromMouse (e);
}

void FragatimeAudioProcessorEditor::mouseDrag (const MouseEvent& e)
{
    if (waveArea.contains (e.getPosition())) setPositionFromMouse (e);
}

//==============================================================================
void FragatimeAudioProcessorEditor::paint (Graphics& g)
{
    const auto p = pal();
    g.fillAll (p.bg);

    // titulo
    g.setColour (p.fg);
    g.setFont (24.f);
    g.drawText ("FRAGATIME", 24, 12, 210, 34, Justification::centredLeft);

    // ---- pantalla de forma de onda ----
    const auto wa = waveArea.toFloat();
    g.setColour (p.grid);
    g.fillRoundedRectangle (wa, 6.f);

    const bool fileMode = (int) std::round (srcP->load()) == 0;
    const bool hasWave = wave.size() >= (size_t) FragatimeAudioProcessor::kPeaks * 2;

    if (fileMode && hasWave)
    {
        const float mid = wa.getCentreY(), hh = wa.getHeight() * 0.46f;
        const int cols = waveArea.getWidth() - 8;
        g.setColour (p.fg.withAlpha (0.55f));
        for (int c = 0; c < cols; ++c)
        {
            const int idx = c * FragatimeAudioProcessor::kPeaks / cols;
            const float mn = wave[(size_t) idx * 2], mx = wave[(size_t) idx * 2 + 1];
            const float x = wa.getX() + 4.f + (float) c;
            g.drawLine (x, mid - mx * hh * 1.6f, x, mid - mn * hh * 1.6f - 0.5f, 1.f);
        }

        // posicion de lectura
        const float cx = wa.getX() + 4.f + proc.centerNorm.load() * (wa.getWidth() - 8.f);
        g.setColour (p.fg.withAlpha (0.18f));
        g.fillRect (cx - 6.f, wa.getY(), 12.f, wa.getHeight());
        g.setColour (p.fg);
        g.drawLine (cx, wa.getY(), cx, wa.getBottom(), 1.5f);

        // granos activos (lineas, como el display de referencia)
        g.setColour (p.fg.withAlpha (0.9f));
        for (auto& a : proc.grainPos)
        {
            const float gp = a.load();
            if (gp < 0.f) continue;
            const float x = wa.getX() + 4.f + gp * (wa.getWidth() - 8.f);
            g.drawLine (x, wa.getY() + 10.f, x, wa.getBottom() - 10.f, 1.f);
        }
    }
    else
    {
        g.setColour (p.dim.withAlpha (0.5f));
        g.drawLine (wa.getX() + 10.f, wa.getCentreY(), wa.getRight() - 10.f, wa.getCentreY(), 1.f);
        g.setColour (p.dim);
        g.setFont (14.f);
        g.drawText (fileMode ? "Drop an audio file here (wav / mp3), or press LOAD AUDIO"
                             : "LIVE  -  the plugin granulates the audio coming into this track",
                    waveArea, Justification::centred);
    }
    g.setColour (p.dim.withAlpha (0.6f));
    g.drawRoundedRectangle (wa, 6.f, 1.f);

    // ---- rotulos de secciones y perillas ----
    g.setFont (10.f);
    g.setColour (p.dim);
    g.drawText ("GRAIN CLOUD", 196, 226, 200, 14, Justification::centredLeft);
    g.drawText ("EFFECTS", 196, 402, 200, 14, Justification::centredLeft);
    g.setColour (p.grid);
    g.drawHorizontalLine (222, 24.f, (float) getWidth() - 24.f);
    g.drawHorizontalLine (398, 196.f, (float) getWidth() - 24.f);
    g.drawVerticalLine (184, 230.f, 560.f);

    g.setFont (11.f);
    g.setColour (p.dim);
    for (size_t i = 0; i < 15; ++i)
    {
        auto b = knobs[i].getBounds();
        g.drawText (kLabels[i], b.getX(), b.getY() - 14, b.getWidth(), 14, Justification::centred);
    }

    // estado del archivo
    g.setColour (p.fg);
    g.setFont (11.f);
    g.drawFittedText (proc.getStatus(), 24, 414, 150, 60, Justification::topLeft, 4);

    g.setColour (p.dim);
    g.setFont (10.f);
    g.drawText ("Click or drag on the waveform to set POSITION  -  PITCH 50% = original  -  V1.0",
                24, 566, getWidth() - 48, 14, Justification::centredLeft);
}
