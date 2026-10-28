#include "PluginEditor.h"
#include "Theme.h"

using namespace fm1c;

namespace
{
constexpr int kMinW = 480;
constexpr float kMinCellScale = 0.4f;

juce::File& lastFolder()
{
    static juce::File folder = []
    {
        const auto docs = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
        return docs.getChildFile ("FM1").isDirectory() ? docs.getChildFile ("FM1") : docs;
    }();
    return folder;
}

juce::String channelText (int ch) { return ch == 0 ? juce::String ("Omni") : "Ch " + juce::String (ch); }
} // namespace

//==============================================================================
FM1CeptionEditor::EngineCount::EngineCount()
{
    setTooltip ("How many FM-1s run, each on a core of its own");
}

void FM1CeptionEditor::EngineCount::setCount (int n)
{
    if (n != count)
    {
        count = n;
        repaint();
    }
}

int FM1CeptionEditor::EngineCount::preferredWidth() const
{
    return 50 + FM1CeptionProcessor::kMaxEngines * 15;
}

juce::Rectangle<float> FM1CeptionEditor::EngineCount::cell (int i) const
{
    const float w = 14.0f, h = 16.0f;
    const float x0 = (float) getWidth() - (float) FM1CeptionProcessor::kMaxEngines * (w + 1.0f);
    return { x0 + (float) i * (w + 1.0f), ((float) getHeight() - h) * 0.5f, w, h };
}

void FM1CeptionEditor::EngineCount::paint (juce::Graphics& g)
{
    g.setColour (theme::dimSilk);
    g.setFont (theme::font (9.5f, true));
    g.drawText ("ENGINES", getLocalBounds().toFloat().withRight (cell (0).getX() - 5.0f),
                juce::Justification::centredRight);

    for (int i = 0; i < FM1CeptionProcessor::kMaxEngines; ++i)
    {
        const auto r = cell (i);
        const bool on = i < count, current = i == count - 1;
        g.setColour (current ? theme::amber : on ? theme::amber.withAlpha (0.35f) : theme::tray);
        g.fillRoundedRectangle (r, 2.5f);
        if (i == hot)
        {
            g.setColour (theme::silk);
            g.drawRoundedRectangle (r.reduced (0.5f), 2.5f, 1.0f);
        }
        g.setColour (current ? theme::header : on ? theme::silk : theme::dimSilk);
        g.setFont (theme::font (10.0f, true));
        g.drawText (juce::String (i + 1), r, juce::Justification::centred);
    }
}

void FM1CeptionEditor::EngineCount::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < FM1CeptionProcessor::kMaxEngines; ++i)
        if (cell (i).expanded (0.5f, 4.0f).contains (e.position) && onChange != nullptr)
            onChange (i + 1);
}

void FM1CeptionEditor::EngineCount::mouseMove (const juce::MouseEvent& e)
{
    int h = -1;
    for (int i = 0; i < FM1CeptionProcessor::kMaxEngines; ++i)
        if (cell (i).expanded (0.5f, 4.0f).contains (e.position))
            h = i;
    if (h != hot)
    {
        hot = h;
        repaint();
    }
}

void FM1CeptionEditor::EngineCount::mouseExit (const juce::MouseEvent&)
{
    hot = -1;
    repaint();
}

//==============================================================================
FM1CeptionEditor::FM1CeptionEditor (FM1CeptionProcessor& p)
    : AudioProcessorEditor (&p), proc (p), meter (p.mainParam())
{
    lnf.setColourScheme (juce::LookAndFeel_V4::getMidnightColourScheme());
    lnf.setColour (juce::PopupMenu::backgroundColourId, theme::header);
    lnf.setColour (juce::PopupMenu::textColourId, theme::silk);
    lnf.setColour (juce::PopupMenu::headerTextColourId, theme::amber);
    lnf.setColour (juce::PopupMenu::highlightedBackgroundColourId, theme::amber.withAlpha (0.85f));
    lnf.setColour (juce::PopupMenu::highlightedTextColourId, theme::header);
    lnf.setColour (juce::TooltipWindow::backgroundColourId, theme::header);
    lnf.setColour (juce::TooltipWindow::textColourId, theme::silk);
    lnf.setColour (juce::TooltipWindow::outlineColourId, theme::tray);
    setLookAndFeel (&lnf);

    menuButton.setColour (juce::TextButton::buttonColourId, theme::tray);
    menuButton.setColour (juce::TextButton::buttonOnColourId, theme::amber);
    menuButton.setColour (juce::TextButton::textColourOffId, theme::silk);
    menuButton.setColour (juce::ComboBox::outlineColourId, theme::tray);
    menuButton.onClick = [this] { showMainMenu(); };
    addAndMakeVisible (menuButton);

    addAndMakeVisible (meter);

    engineCount.onChange = [this] (int n) { proc.setNumEngines (n); };
    addAndMakeVisible (engineCount);

    constrainer.setMinimumSize (kMinW, kHeaderH + 2 * kPad + juce::roundToInt (EngineView::kH * kMinCellScale));
    constrainer.setMaximumSize (8000, 6000);
    setConstrainer (&constrainer);
    setResizable (true, true);

    syncViews();
    const auto saved = proc.getEditorSize();
    const auto size = saved.x > 0 && saved.y > 0 ? saved : naturalSize (proc.getNumEngines());
    setSize (size.x, size.y);

    startTimerHz (30);
}

FM1CeptionEditor::~FM1CeptionEditor()
{
    stopTimer();
    views.clear();
    setConstrainer (nullptr);
    setLookAndFeel (nullptr);
}

//==============================================================================
float FM1CeptionEditor::cellScale() const
{
    if (views.isEmpty() || views[0]->getWidth() <= 0)
        return 1.0f;
    return (float) views[0]->getWidth() / EngineView::kW;
}

juce::Point<int> FM1CeptionEditor::naturalSize (int n) const
{
    const int cols = juce::jmin (n, 4), rows = (n + cols - 1) / cols;
    const int chromeW = (cols + 1) * kPad, chromeH = kHeaderH + (rows + 1) * kPad;

    auto area = juce::Rectangle<int> (1440, 900);
    if (const auto* d = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds()))
        area = d->userArea;
    else if (const auto* primary = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        area = primary->userArea;

    // the cells' current size, shrunk to fit the screen
    const float s = juce::jmax (kMinCellScale,
                                juce::jmin (cellScale(),
                                            ((float) area.getWidth() * 0.92f - (float) chromeW) / ((float) cols * EngineView::kW),
                                            ((float) area.getHeight() * 0.9f - (float) chromeH) / ((float) rows * EngineView::kH)));
    return { juce::jmax (kMinW, chromeW + juce::roundToInt ((float) cols * EngineView::kW * s)),
             chromeH + juce::roundToInt ((float) rows * EngineView::kH * s) };
}

void FM1CeptionEditor::syncViews()
{
    seenLayout = proc.layoutVersion();
    const int n = proc.getNumEngines();

    while (views.size() > n)
        views.removeLast();
    while (views.size() < n)
    {
        auto* v = views.add (new EngineView (proc, views.size()));
        v->onLoadRequest = [this] (int slot) { chooseFile (slot, false); };
        v->onMenuRequest = [this] (int slot) { showEngineMenu (slot); };
        addAndMakeVisible (v);
    }
    for (auto* v : views)
        v->setEngine (proc.engine (v->slot()));
    engineCount.setCount (n);

    const bool changed = seenEngines >= 0 && n != seenEngines;
    seenEngines = n;
    if (changed)
    {
        const auto size = naturalSize (n);
        setSize (size.x, size.y);
    }
    layoutCells();
}

void FM1CeptionEditor::layoutCells()
{
    const int n = views.size();
    if (n == 0)
        return;

    const auto area = getLocalBounds().withTrimmedTop (kHeaderH).reduced (kPad).toFloat();
    int bestCols = 1;
    float best = 0.0f;
    for (int c = 1; c <= n; ++c)
    {
        const int r = (n + c - 1) / c;
        const float s = juce::jmin ((area.getWidth() - (float) (c - 1) * kPad) / ((float) c * EngineView::kW),
                                    (area.getHeight() - (float) (r - 1) * kPad) / ((float) r * EngineView::kH));
        if (s > best + 1.0e-4f)
        {
            best = s;
            bestCols = c;
        }
    }

    const int cols = bestCols, rows = (n + cols - 1) / cols;
    const float w = std::floor (EngineView::kW * best), h = std::floor (EngineView::kH * best);
    const float gridH = (float) rows * h + (float) (rows - 1) * kPad;
    const float y0 = area.getY() + (area.getHeight() - gridH) * 0.5f;

    for (int i = 0; i < n; ++i)
    {
        const int r = i / cols, c = i % cols;
        const int inRow = juce::jmin (cols, n - r * cols);
        const float rowW = (float) inRow * w + (float) (inRow - 1) * kPad;
        const float x0 = area.getX() + (area.getWidth() - rowW) * 0.5f;
        views[i]->setBounds (juce::Rectangle<float> (x0 + (float) c * (w + kPad), y0 + (float) r * (h + kPad), w, h)
                                 .toNearestInt());
    }
}

void FM1CeptionEditor::resized()
{
    auto header = getLocalBounds().removeFromTop (kHeaderH).reduced (8, 4);
    header.removeFromLeft (88);   // the title
    menuButton.setBounds (header.removeFromLeft (48));
    engineCount.setBounds (header.removeFromRight (engineCount.preferredWidth()));
    header.removeFromLeft (10);
    header.removeFromRight (10);
    meter.setBounds (header.withSizeKeepingCentre (juce::jmin (header.getWidth(), 200), header.getHeight()));

    layoutCells();
    proc.setEditorSize ({ getWidth(), getHeight() });
}

void FM1CeptionEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::room);
    const auto header = getLocalBounds().removeFromTop (kHeaderH);
    g.setColour (theme::header);
    g.fillRect (header);
    g.setColour (juce::Colours::black);
    g.fillRect (header.withTop (header.getBottom() - 1));

    g.setColour (theme::silk);
    g.setFont (theme::font (13.0f, true));
    g.drawText ("FM-1Ception", juce::Rectangle<int> (8, 0, 88, kHeaderH), juce::Justification::centredLeft);
}

//==============================================================================
void FM1CeptionEditor::timerCallback()
{
    if (proc.layoutVersion() != seenLayout)
        syncViews();

    const bool shift = juce::ModifierKeys::getCurrentModifiersRealtime().isShiftDown();
    for (auto* v : views)
    {
        v->setShiftHeld (shift && v->isMouseOverOrDragging (true));
        v->refresh();
    }
    meter.push (proc.takePeaks());
}

//==============================================================================
juce::String FM1CeptionEditor::engineLabel (int slot) const
{
    juce::String name;
    if (auto h = proc.engine (slot))
        name = h->firmwareName();
    return "Engine " + juce::String (slot + 1) + "  -  " + (name.isEmpty() ? juce::String ("empty") : name);
}

juce::PopupMenu FM1CeptionEditor::channelMenu (int slot)
{
    juce::PopupMenu m;
    auto* p = &proc;
    const int rx = proc.getReceiveChannel (slot), tx = proc.getFirmwareChannel (slot);
    m.addSectionHeader ("Receive");
    for (int ch = 0; ch <= 16; ++ch)
        m.addItem (channelText (ch), true, ch == rx, [p, slot, ch] { p->setReceiveChannel (slot, ch); });

    juce::PopupMenu fw;
    fw.addItem ("As received", true, tx == 0, [p, slot] { p->setFirmwareChannel (slot, 0); });
    for (int ch = 1; ch <= 16; ++ch)
        fw.addItem ("Ch " + juce::String (ch), true, ch == tx, [p, slot, ch] { p->setFirmwareChannel (slot, ch); });
    m.addSeparator();
    m.addSubMenu ("Send to Firmware On" + juce::String (tx == 0 ? "" : "  (Ch " + juce::String (tx) + ")"), fw);
    return m;
}

juce::PopupMenu FM1CeptionEditor::clockMenu (int slot)
{
    juce::PopupMenu m;
    auto* p = &proc;
    const int def = (int) fm1e_default_mhz();
    for (const int mhz : FM1CeptionProcessor::kClocks)
    {
        bool ticked = true;
        for (int i = 0; i < proc.getNumEngines(); ++i)
            if (slot < 0 ? proc.getClock (i) != mhz : i == slot && proc.getClock (i) != mhz)
                ticked = false;
        const auto text = juce::String (mhz) + " MHz" + (mhz == def ? "  (the chip's clock, least CPU)" : "");
        m.addItem (text, true, ticked, [p, slot, mhz]
        {
            for (int i = 0; i < p->getNumEngines(); ++i)
                if (slot < 0 || i == slot)
                    p->setClock (i, mhz);
        });
    }
    m.addSeparator();
    m.addItem ("Raise it only if a firmware runs out of cycles: changing it restarts the firmware",
               false, false, nullptr);
    m.addItem (fm1e_jit_available() ? "Firmware code runs compiled (JIT)" : "Firmware code runs interpreted (no JIT here)",
               false, false, nullptr);
    return m;
}

void FM1CeptionEditor::showMainMenu()
{
    const int n = proc.getNumEngines();
    juce::PopupMenu m;
    juce::Component::SafePointer<FM1CeptionEditor> safe (this);

    bool anyLoaded = false;
    std::array<bool, FM1CeptionProcessor::kMaxEngines> loaded {};
    for (int i = 0; i < n; ++i)
        if (auto h = proc.engine (i))
            anyLoaded |= (loaded[(size_t) i] = h->hasFirmware());

    auto perEngine = [&] (const juce::String& title, bool needsFirmware, auto action)
    {
        if (n == 1)
        {
            m.addItem (title + "...", ! needsFirmware || loaded[0], false, [safe, action] { if (safe) action (*safe, 0); });
            return;
        }
        juce::PopupMenu sub;
        for (int i = 0; i < n; ++i)
            sub.addItem (engineLabel (i), ! needsFirmware || loaded[(size_t) i], false,
                         [safe, action, i] { if (safe) action (*safe, i); });
        m.addSubMenu (title, sub);
    };

    perEngine ("Load Firmware", false, [] (FM1CeptionEditor& e, int i) { e.chooseFile (i, false); });
    perEngine ("Export Save", true, [] (FM1CeptionEditor& e, int i) { e.chooseFile (i, true); });

    if (anyLoaded)
    {
        juce::PopupMenu init;
        for (int i = 0; i < n; ++i)
            init.addItem (engineLabel (i), loaded[(size_t) i], false, [p = &proc, i] { p->initEngine (i); });
        m.addSubMenu ("Init Engine", init);
    }

    m.addSeparator();
    if (n == 1)
    {
        m.addSubMenu ("MIDI Channel  (" + channelText (proc.getReceiveChannel (0)) + ")", channelMenu (0));
        m.addSubMenu ("CPU Clock  (" + juce::String (proc.getClock (0)) + " MHz)", clockMenu (0));
    }
    else
    {
        juce::PopupMenu midi, clock;
        clock.addSubMenu ("All Engines", clockMenu (-1));
        clock.addSeparator();
        for (int i = 0; i < n; ++i)
        {
            midi.addSubMenu (engineLabel (i) + "   " + channelText (proc.getReceiveChannel (i)), channelMenu (i));
            clock.addSubMenu (engineLabel (i) + "   " + juce::String (proc.getClock (i)) + " MHz", clockMenu (i));
        }
        m.addSubMenu ("MIDI Channel", midi);
        m.addSubMenu ("CPU Clock", clock);
    }

    m.addSeparator();
    m.addItem (juce::String ("FM-1Ception ") + JucePlugin_VersionString + "  -  fm1-emu " FM1C_EMU_VERSION,
               false, false, nullptr);

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&menuButton));
}

void FM1CeptionEditor::showEngineMenu (int slot)
{
    auto h = proc.engine (slot);
    if (h == nullptr)
        return;
    const bool loaded = h->hasFirmware();
    juce::Component::SafePointer<FM1CeptionEditor> safe (this);

    juce::PopupMenu m;
    m.addSectionHeader (engineLabel (slot));
    m.addItem ("Load Firmware...", [safe, slot] { if (safe) safe->chooseFile (slot, false); });
    m.addItem ("Export Save...", loaded, false, [safe, slot] { if (safe) safe->chooseFile (slot, true); });
    m.addItem ("Init Engine", loaded, false, [p = &proc, slot] { p->initEngine (slot); });
    m.addSeparator();
    m.addSubMenu ("MIDI Channel  (" + channelText (proc.getReceiveChannel (slot)) + ")", channelMenu (slot));
    m.addSubMenu ("CPU Clock  (" + juce::String (proc.getClock (slot)) + " MHz)", clockMenu (slot));

    m.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

void FM1CeptionEditor::chooseFile (int slot, bool exporting)
{
    juce::String name;
    if (auto h = proc.engine (slot))
        name = h->firmwareName();

    const auto start = exporting
        ? lastFolder().getChildFile (juce::File::createLegalFileName (name.isEmpty() ? "FM-1" : name) + ".fm1save")
        : lastFolder();
    chooser = std::make_unique<juce::FileChooser> (
        exporting ? "Export engine " + juce::String (slot + 1) + " as a shared save"
                  : "Load a firmware into engine " + juce::String (slot + 1),
        start, exporting ? "*.fm1save" : "*.fwsc;*.fm1save;*.json");

    const int flags = exporting
        ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
              | juce::FileBrowserComponent::warnAboutOverwriting
        : juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    juce::Component::SafePointer<FM1CeptionEditor> safe (this);
    chooser->launchAsync (flags, [safe, slot, exporting] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (safe == nullptr || file == juce::File())
            return;
        lastFolder() = file.getParentDirectory();
        if (exporting)
            safe->proc.exportFile (slot, file);
        else
            safe->proc.loadFile (slot, file);
    });
}
