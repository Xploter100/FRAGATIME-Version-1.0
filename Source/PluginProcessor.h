#pragma once
#include <JuceHeader.h>
#include <array>
#include <memory>
#include <vector>

class FragatimeAudioProcessor : public juce::AudioProcessor
{
public:
    static constexpr int    kPeaks      = 1024;    // columnas de la forma de onda
    static constexpr double kMaxSeconds = 240.0;   // maximo 4 min de audio cargado (~90 MB)

    FragatimeAudioProcessor();
    ~FragatimeAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "FRAGATIME"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::AudioProcessorValueTreeState apvts;

    // ---- archivo de audio ----
    void loadFile (const juce::File& f);
    bool isLoading() const { return loading.load(); }
    juce::String getStatus() const;
    void getWaveform (std::vector<float>& out) const;
    std::atomic<int> fileVersion { 0 };

    // ---- datos para la interfaz ----
    std::array<std::atomic<float>, 32> grainPos;   // posicion (0..1) de los granos activos en el archivo
    std::atomic<float> centerNorm { 0.f };         // centro de lectura (0..1)

private:
    using Coeffs = juce::dsp::IIR::Coefficients<float>;
    using Filter = juce::dsp::IIR::Filter<float>;

    static constexpr int kGrains = 48;

    struct Grain
    {
        bool active = false, file = true;
        double pos = 0.0, inc = 1.0;
        int age = 0, len = 1;
        float gL = 1.f, gR = 1.f, level = 1.f, sharp = 0.f;
    };

    struct Loader : public juce::Thread
    {
        explicit Loader (FragatimeAudioProcessor& p) : juce::Thread ("FRAGATIME loader"), proc (p) {}
        void run() override { proc.doLoad(); }
        FragatimeAudioProcessor& proc;
    };

    void doLoad();

    // archivo
    Loader loader { *this };
    juce::File pendingFile;
    mutable juce::CriticalSection pendingLock;
    mutable juce::SpinLock fileLock;
    std::shared_ptr<const juce::AudioBuffer<float>> fileBuf;     // escrito por el hilo de carga
    std::vector<float> peaks;
    double fileRate = 44100.0;
    juce::String statusText { "No file loaded - using live input" };
    std::atomic<bool> loading { false };

    std::shared_ptr<const juce::AudioBuffer<float>> activeBuf;   // usado por el hilo de audio
    double activeRate = 44100.0;
    int seenVersion = 0;
    double scanOffset = 0.0;

    // motor
    double fs = 44100.0;
    juce::Random rng;
    std::array<Grain, kGrains> grains;
    float hannT[1026] {}, trapT[1026] {};
    int arpStep = 0;
    float spawnCounter = 0.f;

    // buffer circular (entrada en vivo + feedback)
    std::vector<float> ringL, ringR;
    int ringLen = 0, ringW = 0;

    // efectos
    std::vector<float> wetL, wetR;
    Filter fltL, fltR;
    int fltMode = 0;
    float lastFilt = 999.f;
    int holdCnt = 0;
    float heldL = 0.f, heldR = 0.f;
    std::vector<float> dlyL, dlyR;
    int dlySize = 0, dlyW = 0;
    float dlySm = 0.f, dlpL = 0.f, dlpR = 0.f;
    juce::Reverb reverb;
    float intSm = 0.f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FragatimeAudioProcessor)
};
