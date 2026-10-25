// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#include "app.h"
namespace gui
{
std::wstring text(HWND w)
{
    if (!w)
    {
        return {};
    }
    int n = GetWindowTextLengthW(w);
    std::wstring s(std::size_t(n) + 2, 0);
    if (w == app.editor)
    {
        GETTEXTEX request{};
        request.cb = DWORD(s.size() * 2);
        request.codepage = 1200;
        send(w, EM_GETTEXTEX, WPARAM(&request), LPARAM(s.data()));
    }
    else
    {
        GetWindowTextW(w, s.data(), int(s.size()));
    }
    s.resize(wcslen(s.c_str()));
    if (w == app.editor)
    {
        return wide(workbench::normalizeSourceText(utf8(s)));
    }
    return s;
}
void setText(HWND w, const std::wstring &s)
{
    if (text(w) == s)
    {
        return;
    }
    SetWindowTextW(w, s.c_str());
    if (w == app.editor)
    {
        app.decorationDirty = true;
    }
}
HWND create(const wchar_t *cls, const std::wstring &label, DWORD style, int x, int y, int w, int h,
            HWND parent, int id)
{
    auto translated = translate(label);
    HWND out = CreateWindowExW(0, cls, translated.c_str(), style, x, y, w, h, parent, HMENU(INT_PTR(id)),
                               GetModuleHandleW(nullptr), nullptr);
    if (!out)
    {
        throw std::runtime_error("Cannot create window control");
    }
    if (!wcscmp(cls, L"BUTTON") || !wcscmp(cls, L"STATIC"))
    {
        app.captions[out] = label;
    }
    SetWindowSubclass(out, silentProc, 20, 0);
    return out;
}
void logLine(const std::wstring &s)
{
    std::lock_guard<std::mutex> lock(app.logMutex);
    app.pendingLog.push_back(s);
}
int dialog(const std::wstring &body, const std::wstring &title, UINT flags)
{
    if (app.smoke)
    {
        app.dialogs.emplace_back(title, body);
        if ((flags & 15) == MB_OK)
        {
            return IDOK;
        }
        if (app.dialogAnswers.empty())
        {
            throw std::runtime_error("Unexpected smoke confirmation dialog");
        }
        int answer = app.dialogAnswers.front();
        app.dialogAnswers.pop_front();
        return answer;
    }
    return MessageBoxW(app.hwnd, translate(body).c_str(), translate(title).c_str(), flags);
}
std::pair<Path, std::wstring> source()
{
    auto path = trim(text(app.srcEdit));
    if (checked(app.modeFile))
    {
        if (path.empty())
        {
            throw std::runtime_error("Select a text file");
        }
        auto full = fs::absolute(Path(path));
        return {full, decode(read(full))};
    }
    auto dir = executableDirectory();
    if (!path.empty())
    {
        dir = fs::absolute(Path(path)).parent_path();
    }
    return {dir / (safeName(text(app.editorName)) + L".mus"), text(app.editor)};
}
void updateMode()
{
    bool file = checked(app.modeFile);
    EnableWindow(app.srcEdit, file);
    EnableWindow(app.watchCheck, file);
    send(app.editor, EM_SETREADONLY, file);
    EnableWindow(app.editorName, !file);
}
void rememberSource()
{
    app.referencedPath = text(app.srcEdit);
    try
    {
        app.referencedText = decode(read(app.referencedPath));
    }
    catch (...)
    {
        app.referencedText = text(app.editor);
    }
}
static Path fileDialog(bool save, const Path &initial, const wchar_t *title, const wchar_t *filter)
{
    if (app.smoke)
    {
        if (app.fileChoices.empty())
        {
            throw std::runtime_error("Unexpected smoke file dialog");
        }
        auto target = app.fileChoices.front();
        app.fileChoices.pop_front();
        return target;
    }
    std::vector<wchar_t> buffer(32768, 0);
    auto name = initial.wstring();
    std::copy_n(name.data(), std::min(name.size(), buffer.size() - 1), buffer.data());
    auto directory = initial.parent_path().wstring();
    auto caption = translate(title);
    OPENFILENAMEW of{};
    of.lStructSize = sizeof(of);
    of.hwndOwner = app.hwnd;
    of.lpstrFilter = filter;
    of.lpstrFile = buffer.data();
    of.nMaxFile = DWORD(buffer.size());
    of.lpstrTitle = caption.c_str();
    of.lpstrInitialDir = [&]() -> const wchar_t *
    {
        if (directory.empty())
        {
            return nullptr;
        }
        return directory.c_str();
    }();
    of.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|([&]() { if (save) { return OFN_OVERWRITEPROMPT; } return OFN_FILEMUSTEXIST; }());
    BOOL accepted = FALSE;
    if (save)
    {
        accepted = GetSaveFileNameW(&of);
    }
    else
    {
        accepted = GetOpenFileNameW(&of);
    }
    if (!accepted)
    {
        return {};
    }
    return buffer.data();
}
bool saveSource(bool force)
{
    Path path = text(app.srcEdit);
    if (!force && (!checked(app.modeEdit) || path.empty()))
    {
        return true;
    }
    auto current = text(app.editor), baseline = app.referencedText;
    if (path != app.referencedPath)
    {
        try
        {
            baseline = decode(read(path));
        }
        catch (...)
        {
        }
    }
    if (!force && current == baseline)
    {
        return true;
    }
    if (!force)
    {
        int a = dialog(L"変更した参照ファイルを保存しますか？", L"NOTE-MDX-Workbench", MB_YESNOCANCEL);
        if (a == IDCANCEL)
        {
            return false;
        }
        if (a == IDNO)
        {
            return true;
        }
    }
    if (path.empty())
    {
        path = executableDirectory() / (safeName(text(app.editorName)) + L".mus");
    }
    auto target =
        fileDialog(true, path, L"参照ファイルを保存", L"MML/MUS\0*.mus;*.mml;*.txt\0All files\0*.*\0\0");
    if (target.empty())
    {
        return false;
    }
    std::string original;
    try
    {
        original = read(path);
    }
    catch (...)
    {
    }
    atomicWrite(target, encodeSource(current, original));
    setText(app.srcEdit, target.wstring());
    setText(app.editorName, safeName(target.filename().wstring()));
    rememberSource();
    autosave();
    return true;
}
void browseSource()
{
    auto path = fileDialog(false, {}, L"MML/MUSテキストを選択",
                           L"MML/MUS (*.mus;*.mml)\0*.mus;*.mml\0All files\0*.*\0\0");
    if (path.empty() || path == Path(text(app.srcEdit)) || !saveSource(false))
    {
        return;
    }
    if (checked(app.modeEdit))
    {
        auto value = decode(read(path));
        autosave();
        setText(app.editorName, safeName(path.filename().wstring()));
        setText(app.srcEdit, path.wstring());
        setText(app.editor, value);
        send(app.editor, EM_EMPTYUNDOBUFFER);
        rememberSource();
        autosave();
    }
    else
    {
        setText(app.editorName, safeName(path.filename().wstring()));
        setText(app.srcEdit, path.wstring());
    }
    saveSettings();
}
void browsePdx()
{
    auto path = fileDialog(false, {}, L"PDXを選択", L"PDX (*.pdx)\0*.pdx\0All files\0*.*\0\0");
    if (!path.empty())
    {
        setText(app.pdxEdit, path.wstring());
        saveSettings();
    }
}
void launchPdx()
{
    if (app.core.WB_PdxIsOpen())
    {
        return;
    }
    auto document = source();
    auto original = utf8(document.second);
    auto name = utf8(safeName(text(app.editorName)));
    auto updated = workbench::ensurePcmFileDirective(original, name);
    if (updated.second)
    {
        if (checked(app.modeFile))
        {
            atomicWrite(document.first, encodeSource(wide(updated.first), read(document.first)));
        }
        // Insert the single directive as one undoable edit; retain selection.
        DWORD begin = 0, end = 0;
        send(app.editor, EM_GETSEL, WPARAM(&begin), LPARAM(&end));
        auto a = workbench::normalizeSourceText(original), b = workbench::normalizeSourceText(updated.first);
        std::size_t at = 0;
        while (at < a.size() && at < b.size() && a[at] == b[at])
        {
            ++at;
        }
        auto insertion = workbench::toUtf16(a.substr(0, at)).size();
        auto addition = b.substr(at, b.size() - a.size());
        send(app.editor, EM_SETREADONLY, FALSE);
        send(app.editor, EM_SETSEL, insertion, insertion);
        auto units = wide(addition);
        send(app.editor, EM_REPLACESEL, TRUE, LPARAM(units.c_str()));
        if (begin >= insertion)
        {
            begin += DWORD(units.size());
        }
        if (end >= insertion)
        {
            end += DWORD(units.size());
        }
        send(app.editor, EM_SETSEL, begin, end);
        updateMode();
        document.second = wide(updated.first);
    }
    Path project = document.first.parent_path() /
                   Path(wide(workbench::pdxProjectName(utf8(document.second), name))).filename();
    Path startup = project;
    if (!gui::exists(project))
    {
        Path pdx = project;
        pdx.replace_extension(L".pdx");
        if (gui::exists(pdx))
        {
            startup = pdx;
        }
        else
        {
            std::string projectName = utf8(project.stem().wstring()), escaped;
            for (char c : projectName)
            {
                if (c == '\\')
                {
                    escaped += "\\\\";
                }
                else if (c == '\n')
                {
                    escaped += "\\n";
                }
                else if (c != '\r')
                {
                    escaped += c;
                }
            }
            write(project,
                  "# PDXEditor editable project v2\npdxName=" + escaped +
                      "\nformat=0\nsourceFormat=0\nsourcePdx=\ncurrentBank=0\nbankCount=256\n",
                  CREATE_NEW);
        }
    }
    app.core.require(app.core.WB_OpenPdx(app.hwnd, startup.c_str(),
                                         [&]()
                                         {
                                             if (app.english)
                                             {
                                                 return L"en";
                                             }
                                             return L"ja";
                                         }()));
}
void pollVoice()
{
    char buffer[32768]{};
    int n = app.core.WB_VoiceEdit(buffer, sizeof(buffer));
    if (n <= 0 || n > int(sizeof(buffer)))
    {
        return;
    }
    Json edit = Json::parse(std::string(buffer, n - 1));
    auto doc = source();
    compileFull();
    if (app.sourcePath != doc.first || app.sourceText != doc.second)
    {
        throw std::runtime_error("Cannot resolve the instrument insertion point");
    }
    int anchor = 0, offset = 0;
    Path voiceFile = app.sourcePath;
    for (const auto &voice : app.full["voices"].items)
    {
        if (voice["id"].integer() == edit["id"].integer())
        {
            anchor = voice["endLine"].integer();
            voiceFile = wide(voice["file"].text());
            offset = voice["toneOffset"].integer();
            break;
        }
    }
    auto mml = workbench::voiceWithOffsetScope(edit["mml"].text(), offset);
    std::string updated;
    if (_wcsicmp(voiceFile.lexically_normal().c_str(), app.sourcePath.lexically_normal().c_str()))
    {
        auto original = read(voiceFile);
        atomicWrite(voiceFile,
                    encodeSource(wide(workbench::appendVoiceDefinition(utf8(decode(original)), mml, anchor)),
                                 original));
        updated = workbench::ensureOverwriteDirective(utf8(app.sourceText));
    }
    else
    {
        updated = workbench::appendVoiceDefinition(utf8(app.sourceText), mml, anchor);
    }
    if (checked(app.modeFile))
    {
        atomicWrite(app.sourcePath, encodeSource(wide(updated), read(app.sourcePath)));
    }
    send(app.editor, EM_SETREADONLY, FALSE);
    send(app.editor, EM_SETSEL, 0, -1);
    auto units = wide(updated);
    send(app.editor, EM_REPLACESEL, TRUE, LPARAM(units.c_str()));
    updateMode();
    if (checked(app.modeEdit))
    {
        autosave();
    }
    compileFull();
    logLine(L"MMLに音色を追加しました: " + voiceFile.wstring());
}
} // namespace gui
