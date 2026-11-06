// Exercise the real file/dialog logic against a hidden window, without input injection.
#define RETRONP_TEST
#include "../src/main.cpp"
#include <cstdlib>
#include <iostream>

void check(bool condition,const char* name) {
    if(!condition) { std::cerr << "FAIL: " << name << '\n'; std::exit(1); }
}
#include "render_checks.h"
std::string bytes(const std::filesystem::path& path) {
    std::ifstream f(path,std::ios::binary);
    return {(std::istreambuf_iterator<char>(f)),{}};
}
void fixture(const std::filesystem::path& path,const std::string& contents) {
    std::ofstream f(path,std::ios::binary); f.write(contents.data(),static_cast<std::streamsize>(contents.size()));
}
int main() {
    WNDCLASSW wc{}; wc.hInstance=GetModuleHandleW(nullptr); wc.lpfnWndProc=windowProc; wc.lpszClassName=L"RetroNP.IOTests";
    check(RegisterClassW(&wc)!=0,"register hidden test window");
    App app;
    HWND hwnd=CreateWindowExW(0,wc.lpszClassName,L"",WS_POPUP,0,0,960,660,nullptr,nullptr,wc.hInstance,&app);
    check(hwnd!=nullptr,"create hidden test window");
    auto root=std::filesystem::current_path()/L"build"/L"test-data"/std::to_wstring(GetCurrentProcessId());
    std::filesystem::create_directories(root);
    auto plain=root/L"roundtrip.txt";
    app.editor.reset(); app.editor.replace(L"Espa\u00f1ol \u4e16\u754c\nsecond line\n");
    check(app.write(plain.wstring()),"save UTF-8");
    check(!app.editor.dirty(),"successful save marks document clean");
    check(bytes(plain)=="Espa\xC3\xB1ol \xE4\xB8\x96\xE7\x95\x8C\r\nsecond line\r\n","UTF-8 bytes and CRLF output");
    auto content=app.editor.text; app.editor.reset(); app.load(plain.wstring());
    check(app.editor.text==content && !app.editor.dirty(),"UTF-8 round trip");
    auto bom=root/L"bom.txt"; fixture(bom,"\xEF\xBB\xBFhello\r\nworld"); app.load(bom.wstring());
    check(app.utf8bom && app.editor.text==L"hello\nworld","UTF-8 BOM load");
    app.editor.move(app.editor.text.size(),false); app.editor.replace(L"!"); check(app.write(bom.wstring()),"UTF-8 BOM save");
    check(bytes(bom).compare(0,3,"\xEF\xBB\xBF")==0,"BOM preservation");
    auto wide=root/L"wide.txt"; fixture(wide,std::string("\xFF\xFEh\0i\0\r\0\n\0",10)); app.load(wide.wstring());
    check(app.utf16 && app.editor.text==L"hi\n","UTF-16 LE load");
    check(app.write(wide.wstring()) && bytes(wide)==std::string("\xFF\xFEh\0i\0\r\0\n\0",10),"UTF-16 LE round trip");
    auto big=root/L"big-endian.txt"; fixture(big,std::string("\xFE\xFF\0h\0i",6)); app.load(big.wstring());
    check(app.utf16 && app.editor.text==L"hi","UTF-16 BE load");
    auto binary=root/L"binary.dat"; fixture(binary,std::string("abc\0def",7)); auto old=app.editor.text; auto oldPath=app.path;
    app.load(binary.wstring()); check(app.editor.text==old && app.path==oldPath && app.dialog==Dialog::Message,"binary rejection preserves document"); app.cancelDialog();
    app.load((root/L"does-not-exist.txt").wstring()); check(app.editor.text==old && app.path==oldPath,"failed open preserves document"); app.cancelDialog();
    app.editor.replace(L"changed"); check(!app.write((root/L"missing-folder"/L"file.txt").wstring()) && app.editor.dirty(),"failed save keeps dirty content"); app.cancelDialog();
    // Verify the Enter event queued after WM_KEYDOWN cannot leak from a dialog.
    app.utf16=false; app.utf8bom=false; app.show(Dialog::Save,(root/L"enter-test.txt").wstring());
    app.key(VK_RETURN); app.character(L'\r');
    check(app.dialog==Dialog::None && !app.editor.dirty(),"save dialog Enter does not insert a newline");
    // Existing files are only replaced after the custom overwrite prompt is accepted.
    fixture(root/L"existing.txt","keep this"); app.editor.replace(L"!");
    app.show(Dialog::Save,(root/L"existing.txt").wstring()); app.acceptDialog(0);
    check(app.showOverwrite && bytes(root/L"existing.txt")=="keep this","overwrite requires confirmation");
    app.overwriteAnswer(false); check(app.dialog==Dialog::Save && app.editor.dirty(),"declining overwrite returns to Save As"); app.cancelDialog();
    app.request(Pending::New); check(app.dialog==Dialog::Confirm,"new document prompts for unsaved changes");
    app.acceptDialog(2); check(app.editor.dirty() && app.pending==Pending::None,"cancel preserves document");
    app.request(Pending::New); app.acceptDialog(1); check(app.editor.text.empty() && !app.editor.dirty(),"discard then new document");
    checkRendering(app);
    DestroyWindow(hwnd);
    std::cout << "All file and dialog tests passed.\n";
}
