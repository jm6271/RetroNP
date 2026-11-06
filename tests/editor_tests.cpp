#include "../src/editor.h"
#include <cstdlib>
#include <iostream>
void check(bool condition, const char* name) { if (!condition) { std::cerr << "FAIL: " << name << '\n'; std::exit(1); } }
int main() {
    Editor e;
    e.reset(L"one\r\ntwo\rthree");
    check(e.text == L"one\ntwo\nthree", "line ending normalization");
    e.move(3,false); e.replace(L"!"); check(e.dirty(), "dirty after insert");
    e.undo(); check(!e.dirty() && e.text==e.saved, "undo returns to saved content");
    e.redo(); check(e.text==L"one!\ntwo\nthree", "redo restores insertion");
    e.anchor=0; e.caret=4; e.replace(L"1"); check(e.text==L"1\ntwo\nthree", "selection replacement");
    e.undo(); check(e.selection()==L"one!", "undo restores selection");
    e.reset(L"hello world\nnext"); e.move(11,false); e.backspace(); e.del();
    check(e.text==L"hello worlnext", "backspace and newline delete");
    check(e.word(0,1)==6 && e.word(10,-1)==6, "word navigation");
    e.reset(L"Hello hello"); check(e.find(L"HELLO",false) && e.lo()==0, "case insensitive search");
    check(e.find(L"hello",true) && e.lo()==6, "find next");
    check(e.find(L"hello",true) && e.lo()==6, "search wraps");
    check(!e.find(L"missing",false), "not found");
    e.reset(L"abc def gh\n"); auto rows=e.rows(5,true);
    check(rows.size()==4 && rows[0].end==4 && rows.back().start==11, "word wrapping and trailing empty line");
    e.reset(L"\t\tX"); rows=e.rows(8,true); check(rows.size()==3, "tab wrapping");
    e.reset(std::wstring{static_cast<wchar_t>(0xD83D),static_cast<wchar_t>(0xDE00),L'x'});
    check(e.next(0)==2 && e.previous(2)==0, "UTF-16 pair navigation");
    e.move(2,false); e.backspace(); check(e.text==L"x", "UTF-16 pair deletion");
    std::cout << "All editor tests passed.\n";
}
