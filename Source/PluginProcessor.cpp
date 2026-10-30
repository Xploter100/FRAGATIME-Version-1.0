#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
constexpr float kPi = 3.14159265358979f;
const float arpPattern[8] { 0.f, 7.f, 12.f, 16.f, 19.f, 24.f, 19.f, 12.f };   // arpegio mayor ascendente
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout FragatimeAudioProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;

    auto addF = [&] (const char* id, const char* name, float lo, float hi, float def)
    {
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name,
                                                      NormalisableRange<float> (lo, hi, 0.1f), def));
    };

    addF ("intensity", "Intensity", 0.f, 100.f, 70.f);
    addF ("density",   "Density",   0.f, 100.f, 45.f);
    addF ("size",      "Size",      0.f, 100.f, 45.f);
    addF ("pitch",     "Pitch",     0.f, 100.f, 50.f);    // 50 = sin cambio, +-12 semitonos
    addF ("arp",       "Arp",       0.f, 100.f, 0.f);
    addF ("grain",     "Grain",     0.f, 100.f, 35.f);
    addF ("feedback",  "Feedback",  0.f, 100.f, 0.f);
    addF ("position",  "Position",  0.f, 100.f, 0.f);
    addF ("scan",      "Scan",   -100.f, 100.f, 0.f);
    addF ("reverb",    "Reverb",    0.f, 100.f, 15.f);
    addF ("delay",     "Delay",     0.f, 100.f, 10.f);
    addF ("crush",     "Crush",     0.f, 100.f, 0.f);
    addF ("filter",    "Filter", -100.f, 100.f, 0.f);     // negativo = low-pass, positivo = high-pass
    addF ("drive",     "Drive",     0.f, 100.f, 0.f);
    addF ("out",       "Output",  -24.f,   6.f, 0.f);

    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "source", 1 }, "Source", StringArray { "FILE", "LIVE" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "dir", 1 }, "Direction", StringArray { "FWD", "REV", "RND" }, 0));
    return l;
}

FragatimeAudioProcessor::FragatimeAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    for (auto& g : grainPos) g.store (-1.f);
}

FragatimeAudioProcessor::~FragatimeAudioProcessor()
{
    loader.stopThread (4000);
}

bool FragatimeAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    auto o = layouts.getMainOutputChannelSet();
    if (o != juce::AudioChannelSet::mono() && o != juce::AudioChannelSet::stereo()) return false;
    return o == layouts.getMainInputChannelSet();
}

//==============================================================================
// Carga de archivos (hilo aparte, nunca bloquea el audio)
void FragatimeAudioProcessor::loadFile (const juce::File& f)
{
    loader.stopThread (4000);
    { const juce::ScopedLock sl (pendingLock); pendingFile = f; }
    apvts.state.setProperty ("file", f.getFullPathName(), nullptr);
    loader.startThread();
}

void FragatimeAudioProcessor::doLoad()
{
    loading = true;
    juce::File f;
    { const juce::ScopedLock sl (pendingLock); f = pendingFile; }

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (f));

    if (reader == nullptr || reader->lengthInSamples <= 0)
    {
        const juce::SpinLock::ScopedLockType sl (fileLock);
        statusText = "Could not open: " + f.getFileName();
        loading = false;
        return;
    }

    const juce::int64 maxFrames = (juce::int64) (reader->sampleRate * kMaxSeconds);
    const int n = (int) juce::jmin (reader->lengthInSamples, maxFrames);
    auto buf = std::make_shared<juce::AudioBuffer<float>> (2, n);
    reader->read (buf.get(), 0, n, 0, true, true);
    if (reader->numChannels == 1) buf->copyFrom (1, 0, *buf, 0, 0, n);

    // miniatura de la forma de onda (min / max por columna)
    std::vector<float> pk ((size_t) kPeaks * 2, 0.f);
    const float* a = buf->getReadPointer (0);
    const float* b = buf->getReadPointer (1);
    for (int c = 0; c < kPeaks; ++c)
    {
        const int s0 = (int) ((juce::int64) c * n / kPeaks);
        const int s1 = juce::jmax (s0 + 1, (int) ((juce::int64) (c + 1) * n / kPeaks));
        float mn = 0.f, mx = 0.f;
        const int step = juce::jmax (1, (s1 - s0) / 64);
        for (int i = s0; i < s1 && i < n; i += step)
        {
            const float v = 0.5f * (a[i] + b[i]);
            mn = juce::jmin (mn, v); mx = juce::jmax (mx, v);
        }
        pk[(size_t) c * 2] = mn; pk[(size_t) c * 2 + 1] = mx;
    }

    const double secs = (double) n / reader->sampleRate;
    juce::String st = f.getFileName() + "   " + juce::String ((int) secs / 60) + ":"
                      + juce::String ((int) secs % 60).paddedLeft ('0', 2) + "   "
                      + juce::String (reader->sampleRate / 1000.0, 1) + " kHz";
    if (reader->lengthInSamples > maxFrames) st += "   (first 4:00 only)";

    {
        const juce::SpinLock::ScopedLockType sl (fileLock);
        fileBuf = buf;
        peaks = std::move (pk);
        fileRate = reader->sampleRate;
        statusText = st;
    }
    ++fileVersion;
    loading = false;
}

juce::String FragatimeAudioProcessor::getStatus() const
{
    if (loading.load()) return "Loading...";
    const juce::SpinLock::ScopedLockType sl (fileLock);
    return statusText;
}

void FragatimeAudioProcessor::getWaveform (std::vector<float>& out) const
{
    const juce::SpinLock::ScopedLockType sl (fileLock);
    out = peaks;
}

//==============================================================================
void FragatimeAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    fs = sampleRate;
    ringLen = (int) (6.0 * fs);
    ringL.assign ((size_t) ringLen, 0.f);
    ringR.assign ((size_t) ringLen, 0.f);
    ringW = 0;

    dlySize = (int) (2.6 * fs) + 8;
    dlyL.assign ((size_t) dlySize, 0.f);
    dlyR.assign ((size_t) dlySize, 0.f);
    dlyW = 0; dlySm = 0.f; dlpL = dlpR = 0.f;

    const size_t nb = (size_t) juce::jmax (samplesPerBlock, 512);
    wetL.assign (nb, 0.f); wetR.assign (nb, 0.f);

    for (auto& g : grains) g = Grain();
    spawnCounter = 0.f; arpStep = 0; intSm = apvts.getRawParameterValue ("intensity")->load() * 0.01f;
    holdCnt = 0; heldL = heldR = 0.f;
    seenVersion = -1;           // fuerza a recoger el archivo ya cargado
    scanOffset = 0.0;

    // ventanas de grano: suave (Hann) y afilada (trapecio)
    for (int i = 0; i < 1026; ++i)
    {
        const float t = juce::jmin (1.f, (float) i / 1024.f);
        const float s = std::sin (kPi * t);
        hannT[i] = s * s;
        trapT[i] = juce::jlimit (0.f, 1.f, juce::jmin (t / 0.08f, (1.f - t) / 0.08f));
    }

    fltL.coefficients = Coeffs::makeLowPass (fs, 18000.f);
    fltR.coefficients = fltL.coefficients;
    fltL.reset(); fltR.reset();
    fltMode = 0; lastFilt = 999.f;

    reverb.setSampleRate (fs);
    reverb.reset();
}

//==============================================================================
void FragatimeAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int nch = buffer.getNumChannels();
    if (n == 0 || nch == 0 || ringLen == 0) return;

    if ((int) wetL.size() < n) { wetL.resize ((size_t) n); wetR.resize ((size_t) n); }

    // recoger el archivo nuevo (sin bloquear: si el hilo de carga esta escribiendo, se reintenta)
    const int ver = fileVersion.load();
    if (ver != seenVersion)
    {
        const juce::SpinLock::ScopedTryLockType tl (fileLock);
        if (tl.isLocked()) { activeBuf = fileBuf; activeRate = fileRate; seenVersion = ver; scanOffset = 0.0; }
    }
    const bool fileAvail = activeBuf != nullptr && activeBuf->getNumSamples() > 16;
    const int N = fileAvail ? activeBuf->getNumSamples() : 0;
    const float* fL = fileAvail ? activeBuf->getReadPointer (0) : nullptr;
    const float* fR = fileAvail ? activeBuf->getReadPointer (1) : nullptr;

    // ---- parametros ----
    auto P = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    const float intensity = P ("intensity") * 0.01f;
    const float density01 = P ("density") * 0.01f;
    const float size01    = P ("size") * 0.01f;
    const float pitchPct  = P ("pitch");
    const float arp01     = P ("arp") * 0.01f;
    const float grain01   = P ("grain") * 0.01f;
    const float fb01      = P ("feedback") * 0.01f;
    const float pos01     = P ("position") * 0.01f;
    const float scan      = P ("scan") * 0.01f;
    const float reverb01  = P ("reverb") * 0.01f;
    const float delay01   = P ("delay") * 0.01f;
    const float crush01   = P ("crush") * 0.01f;
    const float filt      = P ("filter") * 0.01f;
    const float drive01   = P ("drive") * 0.01f;
    const float outGain   = juce::Decibels::decibelsToGain (P ("out"));
    const int srcMode     = (int) std::round (P ("source"));
    const int dirMode     = (int) std::round (P ("dir"));

    const float densityHz = 2.f * std::pow (60.f, density01);          // 2 .. 120 granos/s
    const float lenMs     = 10.f * std::pow (50.f, size01);            // 10 .. 500 ms
    const int   lenBase   = (int) (lenMs * 0.001f * (float) fs);
    const float baseSemis = (pitchPct - 50.f) / 50.f * 12.f;           // +-12 semitonos
    const float overlap   = densityHz * lenMs * 0.001f;
    const float level     = 0.8f / std::sqrt (juce::jmax (1.f, overlap));
    const int   arpLen    = 2 + (int) (arp01 * 6.f);                   // 2 .. 8 notas
    const float arpProb   = juce::jmin (1.f, arp01 * 3.3f);
    const bool  ringInput = (srcMode == 1) || ! fileAvail;             // entrada en vivo hacia el buffer circular
    const float fbGain    = fb01 * 0.85f;
    const float fbProb    = ringInput ? 0.f : fb01 * 0.7f;

    // tempo del host para el delay (corchea con puntillo)
    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm()) bpm = *b;
    bpm = juce::jlimit (40.0, 300.0, bpm);
    const float delayTarget = (float) (juce::jlimit (0.05, 2.4, 60.0 / bpm * 0.75) * fs);

    // filtro (negativo = low-pass, positivo = high-pass)
    if (filt != lastFilt)
    {
        lastFilt = filt;
        if (filt < -0.02f)
        {
            fltMode = 1;
            fltL.coefficients = Coeffs::makeLowPass (fs, juce::jlimit (60.f, 18000.f, 18000.f * std::pow (0.0125f, -filt)));
            fltR.coefficients = fltL.coefficients;
        }
        else if (filt > 0.02f)
        {
            fltMode = 2;
            fltL.coefficients = Coeffs::makeHighPass (fs, juce::jlimit (20.f, 8000.f, 20.f * std::pow (300.f, filt)));
            fltR.coefficients = fltL.coefficients;
        }
        else fltMode = 0;
    }

    // bitcrusher
    const float bits   = 16.f - crush01 * 12.f;
    const float levels = std::pow (2.f, bits - 1.f);
    const int   holdN  = 1 + (int) (crush01 * crush01 * 24.f);

    const float* inL = buffer.getReadPointer (0);
    const float* inR = nch > 1 ? buffer.getReadPointer (1) : inL;
    const float dgain = 1.f + drive01 * 9.f, dcomp = 1.f / (1.f + drive01 * 0.6f);
    const float dWet = delay01 * 0.8f;

    // ---- crear un grano ----
    auto spawn = [&]
    {
        int slot = -1;
        for (int k = 0; k < kGrains; ++k) if (! grains[(size_t) k].active) { slot = k; break; }
        if (slot < 0) return;

        Grain& g = grains[(size_t) slot];
        g = Grain();
        g.active = true;

        const bool useFile = fileAvail && srcMode == 0 && rng.nextFloat() >= fbProb;

        float semis = baseSemis;
        if (arp01 > 0.001f && rng.nextFloat() < arpProb) { semis += arpPattern[arpStep % arpLen]; ++arpStep; }
        semis += (rng.nextFloat() * 2.f - 1.f) * grain01 * 0.25f;
        semis = juce::jlimit (-24.f, 24.f, semis);
        const float rate = std::pow (2.f, semis / 12.f);

        const float dir = dirMode == 0 ? 1.f : (dirMode == 1 ? -1.f : (rng.nextBool() ? 1.f : -1.f));

        g.len = juce::jmax (64, (int) ((float) lenBase * (1.f + (rng.nextFloat() - 0.5f) * grain01 * 0.6f)));
        g.level = level;
        g.sharp = grain01;

        const float pan = (rng.nextFloat() * 2.f - 1.f) * grain01;
        const float ang = (pan + 1.f) * kPi * 0.25f;
        g.gL = std::cos (ang) * 1.4142f;
        g.gR = std::sin (ang) * 1.4142f;
        g.file = useFile;

        if (useFile)
        {
            g.inc = (double) rate * activeRate / fs * (double) dir;
            const double lenF = (double) g.len * std::abs (g.inc);
            const double center = (double) pos01 * (double) N + scanOffset;
            const double spray = (double) (rng.nextFloat() * 2.f - 1.f) * (0.01 + (double) grain01 * 0.6) * activeRate;
            double start = center + spray;
            if (dir < 0.f) start += lenF;
            start = std::fmod (start, (double) N);
            if (start < 0.0) start += (double) N;
            g.pos = start;
        }
        else
        {
            g.inc = (double) rate * (double) dir;
            const double lenF = (double) g.len * std::abs (g.inc);
            const double maxLook = juce::jmax (0.0, (double) ringLen - 2.0 * lenF - 256.0);
            double look = (double) pos01 * maxLook + (double) rng.nextFloat() * (double) grain01 * 0.3 * fs;
            look = juce::jmin (look, maxLook);
            double start = dir > 0.f ? (double) ringW - 64.0 - lenF - look : (double) ringW - 64.0 - look;
            while (start < 0.0) start += (double) ringLen;
            g.pos = start;
        }
    };

    // ---- bucle principal por muestra ----
    for (int i = 0; i < n; ++i)
    {
        const float xl = inL[i], xr = inR[i];

        if (fileAvail)
        {
            scanOffset += (double) scan * activeRate / fs;
            if (scanOffset >= (double) N) scanOffset -= (double) N;
            else if (scanOffset < 0.0)    scanOffset += (double) N;
        }

        spawnCounter -= 1.f;
        if (spawnCounter <= 0.f)
        {
            spawn();
            spawnCounter += ((float) fs / densityHz) * (1.f + (rng.nextFloat() - 0.5f) * grain01 * 0.9f);
        }

        // suma de granos
        float gl = 0.f, gr = 0.f;
        for (auto& g : grains)
        {
            if (! g.active) continue;

            const float t  = (float) g.age / (float) g.len;
            const float fi = t * 1024.f;
            const int   i0 = (int) fi;
            const float fr = fi - (float) i0;
            const float wh = hannT[i0] + fr * (hannT[i0 + 1] - hannT[i0]);
            const float wt = trapT[i0] + fr * (trapT[i0 + 1] - trapT[i0]);
            const float w  = wh + g.sharp * (wt - wh);

            float sl, sr;
            if (g.file && fileAvail)
            {
                const int ip = (int) g.pos;
                if (g.pos < 0.0 || ip >= N - 1) { g.active = false; continue; }
                const float f = (float) (g.pos - (double) ip);
                sl = fL[ip] + f * (fL[ip + 1] - fL[ip]);
                sr = fR[ip] + f * (fR[ip + 1] - fR[ip]);
            }
            else
            {
                double p = g.pos;
                while (p < 0.0) p += (double) ringLen;
                while (p >= (double) ringLen) p -= (double) ringLen;
                const int ip = (int) p;
                int ip1 = ip + 1; if (ip1 >= ringLen) ip1 = 0;
                const float f = (float) (p - (double) ip);
                sl = ringL[(size_t) ip] + f * (ringL[(size_t) ip1] - ringL[(size_t) ip]);
                sr = ringR[(size_t) ip] + f * (ringR[(size_t) ip1] - ringR[(size_t) ip]);
            }

            const float amp = w * g.level;
            gl += sl * amp * g.gL;
            gr += sr * amp * g.gR;

            g.pos += g.inc;
            if (++g.age >= g.len) g.active = false;
        }

        // buffer circular: entrada en vivo + feedback de la nube de granos
        ringL[(size_t) ringW] = (ringInput ? xl : 0.f) + std::tanh (gl) * fbGain;
        ringR[(size_t) ringW] = (ringInput ? xr : 0.f) + std::tanh (gr) * fbGain;
        if (++ringW >= ringLen) ringW = 0;

        float l = gl, r = gr;

        // drive
        if (drive01 > 0.001f) { l = std::tanh (l * dgain) * dcomp; r = std::tanh (r * dgain) * dcomp; }

        // bitcrusher
        if (crush01 > 0.01f)
        {
            if (--holdCnt <= 0)
            {
                holdCnt = holdN;
                heldL = std::round (l * levels) / levels;
                heldR = std::round (r * levels) / levels;
            }
            l = heldL; r = heldR;
        }

        // filtro
        if (fltMode != 0) { l = fltL.processSample (l); r = fltR.processSample (r); }

        // delay ping-pong sincronizado al tempo
        dlySm += (delayTarget - dlySm) * 0.0005f;
        float rd = (float) dlyW - dlySm;
        if (rd < 0.f) rd += (float) dlySize;
        const int d0 = (int) rd;
        const float df = rd - (float) d0;
        int d1 = d0 + 1; if (d1 >= dlySize) d1 = 0;
        const float wl = dlyL[(size_t) d0] + df * (dlyL[(size_t) d1] - dlyL[(size_t) d0]);
        const float wr = dlyR[(size_t) d0] + df * (dlyR[(size_t) d1] - dlyR[(size_t) d0]);
        dlpL += 0.3f * (wl - dlpL);
        dlpR += 0.3f * (wr - dlpR);
        dlyL[(size_t) dlyW] = l + dlpR * 0.45f;
        dlyR[(size_t) dlyW] = r + dlpL * 0.45f;
        if (++dlyW >= dlySize) dlyW = 0;
        l += wl * dWet; r += wr * dWet;

        wetL[(size_t) i] = l;
        wetR[(size_t) i] = r;
    }

    // ---- reverb sobre la senal procesada ----
    juce::Reverb::Parameters rp;
    rp.roomSize = 0.8f; rp.damping = 0.5f; rp.width = 1.f; rp.freezeMode = 0.f;
    rp.wetLevel = reverb01 * 0.5f;
    rp.dryLevel = 1.f;
    reverb.setParameters (rp);
    reverb.processStereo (wetL.data(), wetR.data(), n);

    // ---- mezcla final (Intensity) ----
    float* outL = buffer.getWritePointer (0);
    float* outR = nch > 1 ? buffer.getWritePointer (1) : nullptr;
    const float target = intensity;
    for (int i = 0; i < n; ++i)
    {
        intSm += (target - intSm) * 0.002f;
        const float dl = inL[i], dr = inR[i];
        const float wl = std::tanh (wetL[(size_t) i] * outGain);
        const float wr = std::tanh (wetR[(size_t) i] * outGain);
        outL[i] = dl * (1.f - intSm) + wl * intSm;
        if (outR != nullptr) outR[i] = dr * (1.f - intSm) + wr * intSm;
    }

    // ---- datos para la interfaz ----
    int k = 0;
    for (auto& g : grains)
        if (g.active && k < 32)
            grainPos[(size_t) k++].store (g.file && N > 0 ? juce::jlimit (0.f, 1.f, (float) (g.pos / (double) N)) : -1.f);
    for (; k < 32; ++k) grainPos[(size_t) k].store (-1.f);
    centerNorm.store (N > 0 ? (float) std::fmod (((double) pos01 * (double) N + scanOffset) / (double) N, 1.0) : 0.f);
}

//==============================================================================
juce::AudioProcessorEditor* FragatimeAudioProcessor::createEditor()
{
    return new FragatimeAudioProcessorEditor (*this);
}

void FragatimeAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void FragatimeAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            const juce::String path = apvts.state.getProperty ("file").toString();
            if (path.isNotEmpty())
            {
                const juce::File f (path);
                if (f.existsAsFile()) loadFile (f);
            }
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FragatimeAudioProcessor();
}
