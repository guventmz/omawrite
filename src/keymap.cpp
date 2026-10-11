#include "keymap.h"

#include <QKeySequence>
#include <QSettings>

namespace {
const QString settingsGroup = QStringLiteral("shortcuts");

QString normalized(const QString &sequence) {
    return QKeySequence(sequence).toString(QKeySequence::PortableText);
}

#ifdef Q_OS_MACOS
constexpr bool macOS = true;
#else
constexpr bool macOS = false;
#endif

// "Ctrl" is Command on macOS and Control elsewhere; "Meta" is the Control
// key on macOS and Super elsewhere.
QStringList on(const QStringList &mac, const QStringList &other) {
    return macOS ? mac : other;
}
}

const QList<Keymap::Action> &Keymap::actions() {
    static const QList<Action> list = {
        {QStringLiteral("goToNote"), QStringLiteral("Go to or create note"), QStringLiteral("Notes"), {QStringLiteral("Ctrl+P")}},
        {QStringLiteral("searchNotes"), QStringLiteral("Search all notes"), QStringLiteral("Notes"), {QStringLiteral("Ctrl+Shift+F")}},
        {QStringLiteral("backlinks"), QStringLiteral("Backlinks"), QStringLiteral("Notes"), {QStringLiteral("Ctrl+Shift+B")}},
        {QStringLiteral("followLink"), QStringLiteral("Follow [[link]] / open image"), QStringLiteral("Notes"),
         {QStringLiteral("Ctrl+Return"), QStringLiteral("Ctrl+Enter")}},
        {QStringLiteral("back"), QStringLiteral("Back"), QStringLiteral("Notes"),
         on({QStringLiteral("Ctrl+[")}, {QStringLiteral("Ctrl+["), QStringLiteral("Alt+Left")})},
        {QStringLiteral("forward"), QStringLiteral("Forward"), QStringLiteral("Notes"),
         on({QStringLiteral("Ctrl+]")}, {QStringLiteral("Ctrl+]"), QStringLiteral("Alt+Right")})},
        {QStringLiteral("journalToday"), QStringLiteral("Today's journal entry"), QStringLiteral("Notes"), {QStringLiteral("Ctrl+Shift+J")}},
        {QStringLiteral("journalPrevious"), QStringLiteral("Previous journal entry"), QStringLiteral("Notes"), {QStringLiteral("Ctrl+Alt+Left")}},
        {QStringLiteral("journalNext"), QStringLiteral("Next journal entry"), QStringLiteral("Notes"), {QStringLiteral("Ctrl+Alt+Right")}},
        {QStringLiteral("fileTree"), QStringLiteral("File tree"), QStringLiteral("Notes"), {QStringLiteral("Ctrl+Shift+E")}},
        {QStringLiteral("preview"), QStringLiteral("Preview in browser"), QStringLiteral("Notes"), {QStringLiteral("Ctrl+Shift+P")}},
        {QStringLiteral("chooseVault"), QStringLiteral("Choose vault folder"), QStringLiteral("Notes"), {QStringLiteral("Ctrl+Shift+O")}},

        {QStringLiteral("bold"), QStringLiteral("Bold"), QStringLiteral("Editing"), {QStringLiteral("Ctrl+B")}},
        {QStringLiteral("italic"), QStringLiteral("Italic"), QStringLiteral("Editing"), {QStringLiteral("Ctrl+I")}},
        {QStringLiteral("markdownLink"), QStringLiteral("Markdown link"), QStringLiteral("Editing"), {QStringLiteral("Ctrl+K")}},
        {QStringLiteral("undo"), QStringLiteral("Undo"), QStringLiteral("Editing"), {QStringLiteral("Ctrl+Z")}},
        {QStringLiteral("redo"), QStringLiteral("Redo"), QStringLiteral("Editing"),
         {QStringLiteral("Ctrl+Shift+Z"), QStringLiteral("Ctrl+Y")}},
        {QStringLiteral("find"), QStringLiteral("Find"), QStringLiteral("Editing"), {QStringLiteral("Ctrl+F")}},
        {QStringLiteral("findNext"), QStringLiteral("Next match"), QStringLiteral("Editing"), {QStringLiteral("Ctrl+G")}},
        {QStringLiteral("replace"), QStringLiteral("Find and replace"), QStringLiteral("Editing"),
         on({QStringLiteral("Ctrl+Alt+F")}, {QStringLiteral("Ctrl+H")})},
        {QStringLiteral("lookUp"), QStringLiteral("Look up word"), QStringLiteral("Editing"),
         on({QStringLiteral("Ctrl+Meta+D")}, {})},

        {QStringLiteral("theme"), QStringLiteral("Theme"), QStringLiteral("View"), {QStringLiteral("Ctrl+Alt+T")}},
        {QStringLiteral("wrap"), QStringLiteral("Wrap: column / window / off"), QStringLiteral("View"), {QStringLiteral("Ctrl+Alt+L")}},
        {QStringLiteral("textBigger"), QStringLiteral("Text bigger"), QStringLiteral("View"),
         {QStringLiteral("Ctrl+="), QStringLiteral("Ctrl++")}},
        {QStringLiteral("textSmaller"), QStringLiteral("Text smaller"), QStringLiteral("View"), {QStringLiteral("Ctrl+-")}},
        {QStringLiteral("textReset"), QStringLiteral("Text default size"), QStringLiteral("View"), {QStringLiteral("Ctrl+0")}},
        {QStringLiteral("interfaceBigger"), QStringLiteral("Interface bigger"), QStringLiteral("View"),
         {QStringLiteral("Ctrl+Alt+="), QStringLiteral("Ctrl+Alt++")}},
        {QStringLiteral("interfaceSmaller"), QStringLiteral("Interface smaller"), QStringLiteral("View"), {QStringLiteral("Ctrl+Alt+-")}},
        {QStringLiteral("interfaceReset"), QStringLiteral("Interface default size"), QStringLiteral("View"), {QStringLiteral("Ctrl+Alt+0")}},
        {QStringLiteral("fullscreen"), QStringLiteral("Fullscreen"), QStringLiteral("View"),
         on({QStringLiteral("Ctrl+Meta+F")}, {QStringLiteral("F11"), QStringLiteral("Meta+F")})},
        {QStringLiteral("vimMode"), QStringLiteral("Vim mode on / off"), QStringLiteral("View"), {QStringLiteral("Ctrl+Alt+V")}},
        {QStringLiteral("shortcuts"), QStringLiteral("Keyboard shortcuts"), QStringLiteral("View"),
         {QStringLiteral("Ctrl+,"), QStringLiteral("Ctrl+?"), QStringLiteral("Ctrl+Shift+?"), QStringLiteral("Ctrl+Shift+/")}},

        {QStringLiteral("save"), QStringLiteral("Save"), QStringLiteral("File"), {QStringLiteral("Ctrl+S")}},
        {QStringLiteral("saveAs"), QStringLiteral("Save as"), QStringLiteral("File"), {QStringLiteral("Ctrl+Shift+S")}},
        {QStringLiteral("open"), QStringLiteral("Open file"), QStringLiteral("File"), {QStringLiteral("Ctrl+O")}},
        {QStringLiteral("newWindow"), QStringLiteral("New window"), QStringLiteral("File"), {QStringLiteral("Ctrl+N")}},
        {QStringLiteral("closeWindow"), QStringLiteral("Close window"), QStringLiteral("File"), on({QStringLiteral("Ctrl+W")}, {})},
        {QStringLiteral("minimize"), QStringLiteral("Minimize"), QStringLiteral("File"), on({QStringLiteral("Ctrl+M")}, {})},
        {QStringLiteral("print"), QStringLiteral("Print"), QStringLiteral("File"), {QStringLiteral("Ctrl+Alt+P")}},
    };
    return list;
}

Keymap::Keymap(QObject *parent) : QObject(parent) {
    QSettings settings;
    settings.beginGroup(settingsGroup);
    for (const Action &action : actions()) {
        if (settings.contains(action.id))
            m_custom.insert(action.id, settings.value(action.id).toStringList());
    }
}

QVariantList Keymap::actionList() const {
    QVariantList list;
    for (const Action &action : actions()) {
        list.append(QVariantMap{{QStringLiteral("id"), action.id},
                                {QStringLiteral("title"), action.title},
                                {QStringLiteral("group"), action.group}});
    }
    return list;
}

QStringList Keymap::sequences(const QString &id) const {
    if (m_custom.contains(id))
        return m_custom.value(id);
    for (const Action &action : actions()) {
        if (action.id == id)
            return action.defaults;
    }
    return {};
}

QVariantMap Keymap::bindings() const {
    QVariantMap map;
    for (const Action &action : actions())
        map.insert(action.id, sequences(action.id));
    return map;
}

// "⇧⌘P" on macOS, "Ctrl+Shift+P" elsewhere; at most two, since the extra
// defaults are spellings of the same keys on other keyboards.
QString Keymap::label(const QStringList &sequences) {
    QStringList shown;
    for (const QString &sequence : sequences) {
        const QString text = QKeySequence(sequence).toString(QKeySequence::NativeText);
        if (!text.isEmpty() && !shown.contains(text))
            shown.append(text);
        if (shown.size() == 2)
            break;
    }
    return shown.isEmpty() ? QStringLiteral("—") : shown.join(QStringLiteral("  "));
}

QVariantMap Keymap::labels() const {
    QVariantMap map;
    for (const Action &action : actions())
        map.insert(action.id, label(sequences(action.id)));
    return map;
}

void Keymap::setRecording(bool recording) {
    if (recording == m_recording)
        return;
    m_recording = recording;
    emit recordingChanged();
}

bool Keymap::isCustom(const QString &id) const {
    return m_custom.contains(id);
}

void Keymap::store(const QString &id, const QStringList &sequences) {
    m_custom.insert(id, sequences);
    QSettings settings;
    settings.beginGroup(settingsGroup);
    settings.setValue(id, sequences);
}

// Gives the action this one sequence. Any other action holding it lets go;
// returns that action's title so the user knows, or nothing.
QString Keymap::assign(const QString &id, const QString &sequence) {
    const QString wanted = normalized(sequence);
    if (wanted.isEmpty())
        return {};
    QString lost;
    for (const Action &action : actions()) {
        if (action.id == id)
            continue;
        QStringList held = sequences(action.id);
        const qsizetype before = held.size();
        held.erase(std::remove_if(held.begin(), held.end(), [&wanted](const QString &s) {
                       return normalized(s) == wanted;
                   }), held.end());
        if (held.size() != before) {
            store(action.id, held);
            lost = action.title;
        }
    }
    store(id, {wanted});
    emit changed();
    return lost;
}

void Keymap::reset(const QString &id) {
    if (!m_custom.remove(id))
        return;
    QSettings settings;
    settings.beginGroup(settingsGroup);
    settings.remove(id);
    emit changed();
}

// The sequence for a key press, or nothing for a lone modifier.
QString Keymap::sequenceFor(int key, int modifiers) {
    if (key == Qt::Key_Shift || key == Qt::Key_Control || key == Qt::Key_Alt || key == Qt::Key_Meta
            || key == Qt::Key_AltGr || key == Qt::Key_CapsLock || key == 0 || key == Qt::Key_unknown)
        return {};
    const Qt::KeyboardModifiers kept = Qt::KeyboardModifiers(modifiers)
        & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    return QKeySequence(QKeyCombination(kept, Qt::Key(key))).toString(QKeySequence::PortableText);
}
