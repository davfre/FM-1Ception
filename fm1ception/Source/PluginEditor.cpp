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

void revealPresets()
{
    const auto folder = FM1CeptionProcessor::presetsFolder();
    folder.createDirectory();
    folder.startAsProcess();
}

/** The presets in `dir` and the folders under it (a few deep) that have any. */
void addPresets (juce::PopupMenu& m, const juce::File& dir, FM1CeptionProcessor* p, int slot, int depth)
{
    auto entries = dir.findChildFiles (juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles, false);
    std::sort (entries.begin(), entries.end(), [] (const juce::File& a, const juce::File& b)
    {
        if (a.isDirectory() != b.isDirectory())
            return a.isDirectory();
        return a.getFileName().compareNatural (b.getFileName()) < 0;
    });
    for (const auto& f : entries)
    {
        if (f.isDirectory())
        {
            juce::PopupMenu sub;
            if (depth < 4)
                addPresets (sub, f, p, slot, depth + 1);
            if (sub.getNumItems() > 0)
                m.addSubMenu (f.getFileName(), sub);
        }
        else if (f.hasFileExtension (FM1CeptionProcessor::kPresetSuffix))
        {
            m.addItem (f.getFileNameWithoutExtension(), [p, slot, f] { p->loadPreset (slot, f); });
        }
    }
}
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
    addChildComponent (tabs);

    constrainer.setMaximumSize (8000, 6000);
    setConstrainer (&constrainer);
    setResizable (true, true);

    syncViews();
    const auto saved = proc.getEditorSize();
    auto size = saved.x > 0 && saved.y > 0 ? saved : naturalSize (proc.getNumEngines());
    if (tabbed())
        size.y = tabbedHeightFor (size.x, cellW(), cellH());
    updateConstrainer();
    setSize (size.x, size.y);
    updateConstrainer();

    startTimerHz (30);
}

FM1CeptionEditor::~FM1CeptionEditor()
{
    stopTimer();
    libraryWindow.reset();
    views.clear();
    setConstrainer (nullptr);
    setLookAndFeel (nullptr);
}

//==============================================================================
float FM1CeptionEditor::cellScale() const
{
    if (views.isEmpty() || views[0]->getWidth() <= 0)
        return 1.0f;
    return (float) views[0]->getWidth() / cellW();
}

juce::Point<int> FM1CeptionEditor::tabbedChrome()
{
    return { 2 * kPad, kHeaderH + kTabsH + 2 * kPad };
}

int FM1CeptionEditor::tabbedHeightFor (int width, float cellW, float cellH)
{
    const auto chrome = tabbedChrome();
    return chrome.y + juce::roundToInt ((float) (width - chrome.x) * cellH / cellW);
}

void FM1CeptionEditor::Constrainer::checkBounds (juce::Rectangle<int>& b, const juce::Rectangle<int>& previous,
                                                 const juce::Rectangle<int>& limits, bool top, bool left,
                                                 bool bottom, bool right)
{
    // the ratio is for hosts that resize the window themselves: the shape here is exact
    const auto ratio = getFixedAspectRatio();
    setFixedAspectRatio (0.0);
    if (keepTabShape)
    {
        if ((top || bottom) && ! (left || right))
        {
            const auto chrome = tabbedChrome();
            b.setWidth (chrome.x + juce::roundToInt ((float) (b.getHeight() - chrome.y) * cellW / cellH));
        }
        else
        {
            b.setHeight (tabbedHeightFor (b.getWidth(), cellW, cellH));
        }
        if (left)
            b.setX (previous.getRight() - b.getWidth());
        if (top)
            b.setY (previous.getBottom() - b.getHeight());
    }
    ComponentBoundsConstrainer::checkBounds (b, previous, limits, top, left, bottom, right);
    if (keepTabShape)
    {
        // a width clamped to the minimum takes the height with it
        b.setHeight (tabbedHeightFor (b.getWidth(), cellW, cellH));
        if (top)
            b.setY (previous.getBottom() - b.getHeight());
    }
    setFixedAspectRatio (ratio);
}

void FM1CeptionEditor::updateConstrainer()
{
    constrainer.keepTabShape = tabbed();
    constrainer.cellW = cellW();
    constrainer.cellH = cellH();
    constrainer.setMinimumSize (kMinW, kHeaderH + 2 * kPad + juce::roundToInt (cellH() * kMinCellScale));
    const int w = juce::jmax (kMinW, getWidth());
    constrainer.setFixedAspectRatio (tabbed() ? (double) w / (double) tabbedHeightFor (w, cellW(), cellH()) : 0.0);
}

juce::Point<int> FM1CeptionEditor::naturalSize (int n, float atLeast) const
{
    const int cols = tabbed() ? 1 : juce::jmin (n, 4), rows = tabbed() ? 1 : (n + cols - 1) / cols;
    const int chromeW = (cols + 1) * kPad;
    const int chromeH = kHeaderH + (tabbed() ? kTabsH : 0) + (rows + 1) * kPad;

    auto area = juce::Rectangle<int> (1440, 900);
    if (const auto* d = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds()))
        area = d->userBounds.toNearestInt();
    else if (const auto* primary = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        area = primary->userBounds.toNearestInt();

    // the cells' current size (tabbed: the one cell as wide as the window is), shrunk to fit the screen
    const float current = tabbed() && getWidth() > 0 ? ((float) getWidth() - (float) chromeW) / cellW() : cellScale();
    const float s = juce::jmax (kMinCellScale,
                                juce::jmin (juce::jmax (current, atLeast),
                                            ((float) area.getWidth() * 0.92f - (float) chromeW) / ((float) cols * cellW()),
                                            ((float) area.getHeight() * 0.9f - (float) chromeH) / ((float) rows * cellH())));
    const int w = juce::jmax (kMinW, chromeW + juce::roundToInt ((float) cols * cellW() * s));
    if (tabbed())
        return { w, tabbedHeightFor (w, cellW(), cellH()) };
    return { w, chromeH + juce::roundToInt ((float) rows * cellH() * s) };
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
        v->onBrowseRequest = [this] (int slot)
        {
            selectEngine (slot);
            showLibrary();
        };
        v->onMenuRequest = [this] (int slot, int lcdRow) { showEngineMenu (slot, lcdRow); };
        v->onSelect = [this] (int slot) { selectEngine (slot); };
        addAndMakeVisible (v);
    }
    const int options = proc.getViewOptions();
    for (auto* v : views)
    {
        v->setEngine (proc.engine (v->slot()));
        v->setViewOptions (options);
        v->setHardware (hardware());
    }
    engineCount.setCount (n);

    const int viewLayout = (int) proc.getViewLayout(), wasViewLayout = seenViewLayout;
    const bool changed = (seenEngines >= 0 && n != seenEngines) || (seenViewLayout >= 0 && viewLayout != seenViewLayout)
                      || (seenViewOptions >= 0 && options != seenViewOptions);
    seenEngines = n;
    seenViewLayout = viewLayout;
    seenViewOptions = options;
    tabs.setVisible (tabbed());
    if (changed)
    {
        // the front panel is busy at the tabbed cell's size: come in at about 1000 wide
        const bool intoHardware = hardware() && viewLayout != wasViewLayout;
        const auto size = naturalSize (n, intoHardware ? 1.55f : 0.0f);
        setSize (size.x, size.y);
    }
    updateConstrainer();
    layoutCells();
    markSelection();
}

void FM1CeptionEditor::selectEngine (int slot)
{
    if (slot == proc.getSelected())
        return;
    proc.setSelected (slot);
    markSelection();
    if (tabbed())
        layoutCells();
}

void FM1CeptionEditor::markSelection()
{
    seenSelected = proc.getSelected();
    for (auto* v : views)
        v->setSelected (! tabbed() && views.size() > 1 && v->slot() == seenSelected);
    tabs.repaint();
}

void FM1CeptionEditor::layoutCells()
{
    const int n = views.size();
    if (n == 0)
        return;

    auto content = getLocalBounds().withTrimmedTop (kHeaderH);
    if (tabbed())
    {
        tabs.setBounds (content.removeFromTop (kTabsH));
        const auto area = content.reduced (kPad).toFloat();
        const float s = juce::jmin (area.getWidth() / cellW(), area.getHeight() / cellH());
        const auto cell = juce::Rectangle<float> (std::floor (cellW() * s), std::floor (cellH() * s))
                              .withCentre (area.getCentre()).toNearestInt();
        const int sel = proc.getSelected();
        for (auto* v : views)
        {
            v->setBounds (cell);
            v->setVisible (v->slot() == sel);
        }
        return;
    }

    for (auto* v : views)
        v->setVisible (true);
    const auto area = content.reduced (kPad).toFloat();
    int bestCols = 1;
    float best = 0.0f;
    for (int c = 1; c <= n; ++c)
    {
        const int r = (n + c - 1) / c;
        const float s = juce::jmin ((area.getWidth() - (float) (c - 1) * kPad) / ((float) c * cellW()),
                                    (area.getHeight() - (float) (r - 1) * kPad) / ((float) r * cellH()));
        if (s > best + 1.0e-4f)
        {
            best = s;
            bestCols = c;
        }
    }

    const int cols = bestCols, rows = (n + cols - 1) / cols;
    const float w = std::floor (cellW() * best), h = std::floor (cellH() * best);
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
    updateConstrainer();
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
    if (proc.getSelected() != seenSelected)
    {
        markSelection();
        if (tabbed())
            layoutCells();
    }

    const bool shift = juce::ModifierKeys::getCurrentModifiersRealtime().isShiftDown();
    for (auto* v : views)
    {
        v->setShiftHeld (shift && v->isMouseOverOrDragging (true));
        v->refresh();
    }
    meter.push (proc.takePeaks());
    if (tabs.isVisible())
        tabs.repaint();
}

//==============================================================================
juce::Rectangle<float> FM1CeptionEditor::Tabs::tab (int i, int n) const
{
    const float gap = 3.0f, x0 = 6.0f;
    const float w = juce::jmin (220.0f, ((float) getWidth() - 2.0f * x0 - (float) (n - 1) * gap) / (float) n);
    return { x0 + (float) i * (w + gap), 4.0f, w, (float) getHeight() - 4.0f };
}

void FM1CeptionEditor::Tabs::paint (juce::Graphics& g)
{
    const int n = owner.views.size(), sel = owner.proc.getSelected();
    for (int i = 0; i < n; ++i)
    {
        const auto* v = owner.views[i];
        const auto r = tab (i, n);
        const bool active = i == sel;
        const auto name = v->firmwareName();

        juce::Path shape;
        shape.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), 5.0f, 5.0f, true, true, false, false);
        g.setColour (active ? theme::header : theme::tray.darker (0.25f));
        g.fillPath (shape);
        if (active)
        {
            g.setColour (theme::amber);
            g.fillRect (r.withHeight (2.0f).reduced (4.0f, 0.0f));
        }

        auto body = r.reduced (6.0f, 3.0f);
        const auto badge = body.removeFromLeft (14.0f).withSizeKeepingCentre (14.0f, 12.0f);
        g.setColour (name.isNotEmpty() ? theme::amber : theme::tray);
        g.fillRoundedRectangle (badge, 2.0f);
        g.setColour (name.isNotEmpty() ? theme::header : theme::dimSilk);
        g.setFont (theme::font (10.0f, true));
        g.drawText (juce::String (i + 1), badge, juce::Justification::centred);

        const auto lvl = body.removeFromRight (juce::jmin (36.0f, body.getWidth() * 0.35f)).withSizeKeepingCentre (36.0f, 7.0f);
        g.setColour (juce::Colour (0xff101113));
        g.fillRect (lvl.expanded (1.0f));
        v->level().paint (g, lvl, false);

        g.setColour (active ? theme::silk : theme::dimSilk);
        g.setFont (theme::font (10.5f, active));
        g.drawText (name.isEmpty() ? juce::String ("empty") : name, body.reduced (5.0f, 0.0f), juce::Justification::centredLeft, true);
    }
}

void FM1CeptionEditor::Tabs::mouseDown (const juce::MouseEvent& e)
{
    const int n = owner.views.size();
    for (int i = 0; i < n; ++i)
        if (tab (i, n).contains (e.position))
        {
            owner.selectEngine (i);
            if (e.mods.isPopupMenu())
                owner.showEngineMenu (i, -1);
            return;
        }
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
    const auto mode = proc.getMidiMode();
    const bool custom = mode == FM1CeptionProcessor::MidiMode::custom;

    m.addSectionHeader (custom ? "Receive" : "Receive  (MIDI Mode > Custom picks this)");
    for (int ch = 0; ch <= 16; ++ch)
        m.addItem (channelText (ch), custom, custom && ch == rx, [p, slot, ch] { p->setReceiveChannel (slot, ch); });

    // Selected mode sends every channel to the firmware as one: Ch 1 unless picked here
    const bool selectedMode = mode == FM1CeptionProcessor::MidiMode::selected;
    juce::PopupMenu fw;
    fw.addItem (selectedMode ? "Ch 1  (default)" : "As received", true, tx == 0, [p, slot] { p->setFirmwareChannel (slot, 0); });
    for (int ch = selectedMode ? 2 : 1; ch <= 16; ++ch)
        fw.addItem ("Ch " + juce::String (ch), true, ch == tx, [p, slot, ch] { p->setFirmwareChannel (slot, ch); });
    m.addSeparator();
    if (mode == FM1CeptionProcessor::MidiMode::multiChannel)
        m.addItem ("Sent to the firmware on Ch 1  (MultiChannel)", false, false, nullptr);
    else
        m.addSubMenu ("Send to Firmware On" + juce::String (tx == 0 ? "" : "  (Ch " + juce::String (tx) + ")"), fw);
    return m;
}

juce::String FM1CeptionEditor::routeText (int slot) const
{
    switch (proc.getMidiMode())
    {
        case FM1CeptionProcessor::MidiMode::selected:
            return slot == proc.getSelected() ? "all channels" : "-";
        case FM1CeptionProcessor::MidiMode::multiChannel:
            return "Ch " + juce::String (slot + 1);
        case FM1CeptionProcessor::MidiMode::custom:
            break;
    }
    const int tx = proc.getFirmwareChannel (slot);
    return channelText (proc.getReceiveChannel (slot)) + (tx > 0 ? " > Ch " + juce::String (tx) : juce::String());
}

juce::PopupMenu FM1CeptionEditor::midiMenu()
{
    using Mode = FM1CeptionProcessor::MidiMode;
    juce::PopupMenu m;
    auto* p = &proc;
    const auto mode = proc.getMidiMode();
    const int n = proc.getNumEngines();

    m.addSectionHeader ("Mode");
    m.addItem ("Selected Engine  -  every channel plays the engine you clicked", true, mode == Mode::selected,
               [p] { p->setMidiMode (Mode::selected); });
    m.addItem ("MultiChannel  -  Ch N plays engine N, sent to it as Ch 1", true, mode == Mode::multiChannel,
               [p] { p->setMidiMode (Mode::multiChannel); });
    m.addItem ("Custom  -  each engine's own channels", true, mode == Mode::custom,
               [p] { p->setMidiMode (Mode::custom); });

    m.addSeparator();
    m.addSectionHeader ("Channels");
    for (int i = 0; i < n; ++i)
        m.addSubMenu (engineLabel (i) + "   " + routeText (i), channelMenu (i), mode != Mode::multiChannel);
    return m;
}

juce::String FM1CeptionEditor::midiModeText() const
{
    switch (proc.getMidiMode())
    {
        case FM1CeptionProcessor::MidiMode::selected:     return "Selected Engine";
        case FM1CeptionProcessor::MidiMode::multiChannel: return "MultiChannel";
        case FM1CeptionProcessor::MidiMode::custom:       break;
    }
    return "Custom";
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
    m.addItem ("Firmware Library...", [safe] { if (safe) safe->showLibrary(); });
    if (n == 1)
    {
        m.addSubMenu ("Presets", presetsMenu (0, true));
    }
    else
    {
        juce::PopupMenu presets;
        for (int i = 0; i < n; ++i)
            presets.addSubMenu (engineLabel (i), presetsMenu (i, false));
        presets.addSeparator();
        presets.addItem ("Open Presets Folder", [] { revealPresets(); });
        m.addSubMenu ("Presets", presets);
    }
    perEngine ("Save Preset (Machine State)", true, [] (FM1CeptionEditor& e, int i) { e.savePresetAs (i); });
    perEngine ("Export Save", true, [] (FM1CeptionEditor& e, int i) { e.chooseFile (i, true); });

    if (anyLoaded)
    {
        juce::PopupMenu init;
        for (int i = 0; i < n; ++i)
            init.addItem (engineLabel (i), loaded[(size_t) i], false, [p = &proc, i] { p->initEngine (i); });
        m.addSubMenu ("Init Engine", init);
    }

    m.addSeparator();
    m.addSubMenu ("MIDI  (" + midiModeText() + ")", midiMenu());
    if (n == 1)
    {
        m.addSubMenu ("CPU Clock  (" + juce::String (proc.getClock (0)) + " MHz)", clockMenu (0));
    }
    else
    {
        juce::PopupMenu clock;
        clock.addSubMenu ("All Engines", clockMenu (-1));
        clock.addSeparator();
        for (int i = 0; i < n; ++i)
            clock.addSubMenu (engineLabel (i) + "   " + juce::String (proc.getClock (i)) + " MHz", clockMenu (i));
        m.addSubMenu ("CPU Clock", clock);
    }

    using Layout = FM1CeptionProcessor::ViewLayout;
    using Option = FM1CeptionProcessor::ViewOption;
    juce::PopupMenu view;
    view.addSectionHeader ("Layout");
    const auto layout = proc.getViewLayout();
    view.addItem ("Tiled  -  every engine at once", true, layout == Layout::tiled, [p = &proc] { p->setViewLayout (Layout::tiled); });
    view.addItem ("Tabbed  -  one engine at a time", true, layout == Layout::tabbed, [p = &proc] { p->setViewLayout (Layout::tabbed); });
    view.addItem ("Hardware (Tabbed)  -  the FM-1's front panel, one engine at a time", true, layout == Layout::hardware,
                  [p = &proc] { p->setViewLayout (Layout::hardware); });
    view.addSeparator();
    view.addSectionHeader (hardware() ? "Each Engine Shows  (not with Hardware)" : "Each Engine Shows");
    auto option = [&view, p = &proc, panel = hardware()] (const juce::String& text, Option o)
    {
        const bool on = p->hasViewOption (o);
        view.addItem (text, ! panel, on, [p, o, on] { p->setViewOption (o, ! on); });
    };
    option ("Screens Only  -  no knobs under the screen", Option::screensOnly);
    option ("Edit Buttons Show  -  the panel buttons in a row of their own", Option::editButtons);
    option ("Large Keyboard  -  the keys across the whole engine", Option::largeKeyboard);
    m.addSubMenu (juce::String ("View  (") + (hardware() ? "Hardware" : tabbed() ? "Tabbed" : "Tiled") + ")", view);

    const bool multi = proc.isMultiOut();
    juce::PopupMenu outputs;
    outputs.addItem ("Stereo Mix  -  every engine in the main output", true, ! multi, [p = &proc] { p->setMultiOut (false); });
    outputs.addItem ("Multi-Out  -  engine N plays out of output pair N", true, multi, [p = &proc] { p->setMultiOut (true); });
    outputs.addSeparator();
    juce::StringArray enabled;
    for (int b = 0; b < FM1CeptionProcessor::kMaxEngines; ++b)
        if (proc.isOutputEnabled (b))
            enabled.add (juce::String (b + 1));
    outputs.addItem ("Pairs the host has enabled: " + enabled.joinIntoString (" "), false, false, nullptr);
    outputs.addItem ("An engine whose pair is off plays in the main output", false, false, nullptr);
    m.addSubMenu (juce::String ("Outputs  (") + (multi ? "Multi-Out" : "Stereo Mix") + ")", outputs);

    m.addSeparator();
    m.addItem (juce::String ("FM-1Ception ") + JucePlugin_VersionString + "  -  fm1-emu " FM1C_EMU_VERSION,
               false, false, nullptr);

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&menuButton));
}

void FM1CeptionEditor::showEngineMenu (int slot, int lcdRow)
{
    auto h = proc.engine (slot);
    if (h == nullptr || ! juce::isPositiveAndBelow (slot, views.size()))
        return;
    const bool loaded = h->hasFirmware();
    juce::Component::SafePointer<FM1CeptionEditor> safe (this);
    juce::Component::SafePointer<EngineView> view (views[slot]);

    juce::PopupMenu m;
    m.addSectionHeader (engineLabel (slot));
    m.addItem ("Load Firmware...", [safe, slot] { if (safe) safe->chooseFile (slot, false); });
    m.addItem ("Firmware Library...", [safe] { if (safe) safe->showLibrary(); });
    m.addSubMenu ("Presets", presetsMenu (slot, true));
    m.addItem ("Save Preset (Machine State)...", loaded, false, [safe, slot] { if (safe) safe->savePresetAs (slot); });
    m.addItem ("Export Save...", loaded, false, [safe, slot] { if (safe) safe->chooseFile (slot, true); });
    m.addItem ("Init Engine", loaded, false, [p = &proc, slot] { p->initEngine (slot); });
    m.addItem ("Duplicate to All Engines", loaded && proc.getNumEngines() > 1, false,
               [safe, slot] { if (safe) safe->confirmDuplicate (slot); });

    // soft knobs: where this firmware draws KNOB1..4 on its screen, kept per firmware
    const int soft = views[slot]->softKnobRow();
    if (loaded && (lcdRow >= 0 || soft >= 0))
    {
        m.addSeparator();
        if (lcdRow >= 0)
            m.addItem ("Set Knob Y Value Here  (row " + juce::String (lcdRow) + ")",
                       [view, lcdRow] { if (view) view->setSoftKnobRow (lcdRow); });
        if (soft >= 0)
            m.addItem ("Remove Knobs Set Position  (row " + juce::String (soft) + ")",
                       [view] { if (view) view->setSoftKnobRow (-1); });
    }

    m.addSeparator();
    m.addSubMenu ("MIDI Channel  (" + routeText (slot) + ")", channelMenu (slot),
                  proc.getMidiMode() != FM1CeptionProcessor::MidiMode::multiChannel);
    m.addSubMenu ("CPU Clock  (" + juce::String (proc.getClock (slot)) + " MHz)", clockMenu (slot));
    if (proc.isMultiOut())
        m.addItem (slot == 0 ? juce::String ("Output: Main")
                   : proc.isOutputEnabled (slot) ? "Output: Engine " + juce::String (slot + 1)
                                                 : "Output: Main  (enable the plug-in's Engine " + juce::String (slot + 1)
                                                       + " output in the host)",
                   false, false, nullptr);

    m.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

void FM1CeptionEditor::confirmDuplicate (int slot)
{
    const auto targets = proc.duplicateTargets (slot);
    if (targets.isEmpty())
    {
        proc.duplicateToAll (slot);
        return;
    }

    juce::StringArray names;
    for (int t : targets)
        names.add (engineLabel (t));
    const auto options = juce::MessageBoxOptions::makeOptionsOkCancel (
        juce::MessageBoxIconType::QuestionIcon, "Duplicate " + engineLabel (slot) + " to all engines?",
        "This replaces the firmware, its saved settings, the CPU clock and the MASTER level of:\n\n"
            + names.joinIntoString ("\n"),
        "Duplicate", "Cancel", this);
    juce::Component::SafePointer<FM1CeptionEditor> safe (this);
    confirmBox = juce::AlertWindow::showScopedAsync (options, [safe, slot] (int result)
    {
        if (safe != nullptr && result == 1)
            safe->proc.duplicateToAll (slot);
    });
}

juce::PopupMenu FM1CeptionEditor::presetsMenu (int slot, bool withFolderItem)
{
    juce::PopupMenu m;
    addPresets (m, FM1CeptionProcessor::presetsFolder(), &proc, slot, 0);
    if (m.getNumItems() == 0)
        m.addItem ("No presets yet: Save Preset adds one", false, false, nullptr);
    if (withFolderItem)
    {
        m.addSeparator();
        m.addItem ("Open Presets Folder", [] { revealPresets(); });
    }
    return m;
}

void FM1CeptionEditor::savePresetAs (int slot)
{
    juce::String name;
    if (auto h = proc.engine (slot))
        name = h->firmwareName();
    const auto legal = juce::File::createLegalFileName (name.isEmpty() ? "FM-1" : name);
    // a folder per firmware, so the Presets menu sorts them by it
    const auto folder = FM1CeptionProcessor::presetsFolder().getChildFile (legal);
    folder.createDirectory();

    chooser = std::make_unique<juce::FileChooser> (
        "Save engine " + juce::String (slot + 1) + " as a preset: its firmware and the whole machine as it is",
        folder.getNonexistentChildFile (legal, FM1CeptionProcessor::kPresetSuffix, false),
        juce::String ("*") + FM1CeptionProcessor::kPresetSuffix);

    juce::Component::SafePointer<FM1CeptionEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe, slot] (const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (safe == nullptr || file == juce::File())
            return;
        if (! file.hasFileExtension (FM1CeptionProcessor::kPresetSuffix))
            file = file.withFileExtension (FM1CeptionProcessor::kPresetSuffix);
        safe->proc.savePreset (slot, file);
    });
}

void FM1CeptionEditor::showLibrary()
{
    if (libraryWindow == nullptr)
    {
        LibraryView::Engines engines;
        engines.count = [this] { return proc.getNumEngines(); };
        engines.selected = [this] { return proc.getSelected(); };
        engines.label = [this] (int slot) { return engineLabel (slot); };
        engines.load = [this] (int slot, const juce::File& file)
        {
            proc.loadFile (slot, file);
            selectEngine (slot);
        };
        libraryWindow = std::make_unique<LibraryWindow> (library->library, &library->cloud, std::move (engines), &library->screens);
    }
    libraryWindow->show();
}

void FM1CeptionEditor::chooseFile (int slot, bool exporting)
{
    juce::String name;
    if (auto h = proc.engine (slot))
        name = h->firmwareName();

    // loading starts in the firmware library's folder (or where it was last, if that is in it)
    auto loadFrom = lastFolder();
    if (const auto root = library->library.getRoot(); root.isDirectory() && loadFrom != root && ! loadFrom.isAChildOf (root))
        loadFrom = root;
    const auto start = exporting
        ? lastFolder().getChildFile (juce::File::createLegalFileName (name.isEmpty() ? "FM-1" : name) + ".fm1save")
        : loadFrom;
    chooser = std::make_unique<juce::FileChooser> (
        exporting ? "Export engine " + juce::String (slot + 1) + " as a shared save"
                  : "Load a firmware into engine " + juce::String (slot + 1),
        start, exporting ? "*.fm1save" : "*.fwsc;*.fm1save;*.json;*.fm1ception");

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
