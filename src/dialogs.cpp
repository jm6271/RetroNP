#include "app.h"
#include <filesystem>

namespace retronp {

void App::show(Dialog kind, const std::wstring& value) {
    // Dialog fields reuse the document editor, including selection and undo.
    // Release mouse capture so a document drag cannot continue behind a dialog.
    dialog = kind;
    activeMenu = -1;
    hover = -1;
    dialogFocus = 0;
    input.reset(value);
    input.anchor = 0;
    input.caret = input.text.size();
    blink = true;
    ReleaseCapture();
    selecting = false;
    invalidate();
}

void App::info(const std::wstring& body, const std::wstring& title) {
    message = body;
    messageTitle = title;
    show(Dialog::Message);
    dialogFocus = 1;
}

void App::cancelDialog() {
    dialog = Dialog::None;
    pending = Pending::None;
    inputSelecting = false;
    showOverwrite = false;
    invalidate();
}

void App::acceptDialog(int button) {
    // Button indices are zero-based; dialogFocus reserves zero for the field
    // and uses one-based indices for buttons.
    if (dialog == Dialog::Confirm) {
        if (button == 0) {
            dialog = Dialog::None;
            save();
        } else if (button == 1) {
            dialog = Dialog::None;
            continuePending();
        } else
            cancelDialog();
    } else if (dialog == Dialog::Message || dialog == Dialog::About)
        cancelDialog();
    else if (button != 0)
        cancelDialog();
    else if (dialog == Dialog::Find) {
        query = input.text;
        if (!query.empty()) {
            dialog = Dialog::None;
            findNext();
        }
    } else {
        std::wstring filename = input.text;
        if (filename.size() >= 2 && filename.front() == L'"' && filename.back() == L'"')
            filename = filename.substr(1, filename.size() - 2);
        if (filename.empty())
            return;
        Dialog kind = dialog;
        dialog = Dialog::None;
        if (kind == Dialog::Open)
            load(filename);
        else {
            // An existing target different from the current document needs explicit overwrite consent.
            std::error_code ec;
            bool exists = std::filesystem::exists(filename, ec);
            bool same = false;
            if (!path.empty())
                same = std::filesystem::equivalent(filename, path, ec);
            if (exists && !same) {
                overwritePath = filename;
                message = L"\"" + std::filesystem::path(filename).filename().wstring() +
                          L"\" already exists. Replace it?";
                showOverwrite = true;
                show(Dialog::Message);
                messageTitle = L"Confirm Save As";
                dialogFocus = 2;
            } else if (write(filename))
                continuePending();
            else
                pending = Pending::None;
        }
    }
    invalidate();
}

void App::overwriteAnswer(bool yes) {
    showOverwrite = false;
    dialog = Dialog::None;
    if (yes) {
        if (write(overwritePath))
            continuePending();
        else
            pending = Pending::None;
    } else
        show(Dialog::Save, overwritePath);
    invalidate();
}

} // namespace retronp
