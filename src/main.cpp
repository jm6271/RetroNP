#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "editor.h"

namespace {
constexpr COLORREF face=RGB(192,192,192), light=RGB(255,255,255), shadow=RGB(128,128,128), dark=RGB(0,0,0), blue=RGB(0,0,128);
// One pixel grid is shared by the caption, About dialog, and Windows app icon.
constexpr const char* notepadPixels[]={
    "   ###########  ",
    "  #W#W#W#W#WWW# ",
    " ##W#W#W#W#WWW# ",
    " #WWWWWWWWWWWW# ",
    " #WWWWWWWWWWWW#G",
    " #WW########WW#G",
    " #WWWWWWWWWWWW#G",
    " #WW########WW#G",
    " #WWWWWWWWWWWW#G",
    " #WW########WW#G",
    " #WWWWWWWWWWWW#G",
    " #WW########WW#G",
    " #WWWWWWWWWWWW#G",
    " #WWWWWWWWWWWW#G",
    " ##############G",
    "  GGGGGGGGGGGGGG"
};
[[maybe_unused]] HICON createNotepadIcon() {
    BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=16; info.bmiHeader.biHeight=-16;
    info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr; HBITMAP color=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!color) return nullptr;
    auto pixels=static_cast<DWORD*>(bits);
    for(int y=0;y<16;++y) for(int x=0;x<16;++x) {
        char p=notepadPixels[y][x]; pixels[y*16+x]=p==' '?0:p=='W'?0xFFFFFFFF:p=='G'?0xFF808080:0xFF000000;
    }
    WORD maskBits[16]{}; HBITMAP mask=CreateBitmap(16,16,1,1,maskBits);
    ICONINFO icon{}; icon.fIcon=TRUE; icon.hbmColor=color; icon.hbmMask=mask;
    HICON result=CreateIconIndirect(&icon); DeleteObject(color); DeleteObject(mask); return result;
}
enum Command { New=1, Open, Save, SaveAs, Exit, Undo, Redo, Cut, Copy, Paste, Delete, SelectAll, TimeDate, Wrap, Find, FindNext, About };
enum class Dialog { None, Open, Save, Find, Confirm, Message, About };
enum class Pending { None, New, Open, Exit, Drop };
struct Item { std::wstring label, shortcut; int command; };
struct Menu { std::wstring label; int x, width; std::vector<Item> items; };
struct Scroll { RECT track{}, thumb{}, before{}, after{}; int max=0, page=1; };
HBITMAP pixelBitmap(HDC reference,int width,int height) {
    BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=width; info.bmiHeader.biHeight=-height;
    info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
    void* pixels=nullptr; return CreateDIBSection(reference,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
}
struct FrameBuffer {
    HDC dc=nullptr; HBITMAP bitmap=nullptr; HGDIOBJ original=nullptr; int width=0,height=0;
    FrameBuffer()=default;
    FrameBuffer(const FrameBuffer&)=delete;
    FrameBuffer& operator=(const FrameBuffer&)=delete;
    void release() {
        if(dc && original) SelectObject(dc,original);
        if(bitmap) DeleteObject(bitmap);
        if(dc) DeleteDC(dc);
        dc=nullptr; bitmap=nullptr; original=nullptr; width=height=0;
    }
    bool ensure(HDC reference,int w,int h) {
        if(dc && w==width && h==height) return true;
        release(); dc=CreateCompatibleDC(reference); if(!dc) return false;
        bitmap=pixelBitmap(reference,w,h); if(!bitmap) { release(); return false; }
        original=SelectObject(dc,bitmap); width=w; height=h; return true;
    }
    ~FrameBuffer() { release(); }
};
struct App {
    HWND hwnd=nullptr;
    Editor editor, input;
    std::wstring path, droppedPath, query, message, messageTitle;
    bool utf16=false, utf8bom=false, wrap=false, selecting=false, focused=true, blink=true, eatCharacter=false;
    int width=960, height=640, dpi=96, cw=8, lh=16, top=0, left=0, preferredColumn=-1;
    int activeMenu=-1, hover=-1, titlePress=-1, scrollDrag=0, dragOffset=0;
    int dialogFocus=0; bool inputSelecting=false;
    Dialog dialog=Dialog::None; Pending pending=Pending::None;
    HFONT uiFont=nullptr, titleFont=nullptr, textFont=nullptr;
    std::vector<std::wstring> privateFonts;
    HDC canvas=nullptr;
    HBITMAP canvasBitmap=nullptr;
    HGDIOBJ originalBitmap=nullptr;
    int canvasWidth=0,canvasHeight=0;
    bool canvasDirty=true;
    FrameBuffer frame;
    std::wstring windowTitle;
    std::vector<Row> rows;
    Scroll vertical, horizontal;
    std::vector<Menu> menus{
        {L"&File",6,32,{{L"&New",L"Ctrl+N",New},{L"&Open...",L"Ctrl+O",Open},{L"&Save",L"Ctrl+S",Save},{L"Save &As...",L"Ctrl+Shift+S",SaveAs},{L"",L"",0},{L"E&xit",L"Alt+F4",Exit}}},
        {L"&Edit",38,34,{{L"&Undo",L"Ctrl+Z",Undo},{L"&Redo",L"Ctrl+Y",Redo},{L"",L"",0},{L"Cu&t",L"Ctrl+X",Cut},{L"&Copy",L"Ctrl+C",Copy},{L"&Paste",L"Ctrl+V",Paste},{L"De&lete",L"Del",Delete},{L"",L"",0},{L"Select &All",L"Ctrl+A",SelectAll},{L"Time/&Date",L"F5",TimeDate},{L"",L"",0},{L"&Word Wrap",L"",Wrap}}},
        {L"&Search",72,50,{{L"&Find...",L"Ctrl+F",Find},{L"Find &Next",L"F3",FindNext}}},
        {L"&Help",122,36,{{L"&About RetroNP",L"",About}}}
    };
    RECT editorRect() const { return {6,46,width-24,height-24}; }
    int columns() const { auto r=editorRect(); return std::max<int>(1,(r.right-r.left-6)/cw); }
    int page() const { auto r=editorRect(); return std::max<int>(1,(r.bottom-r.top-4)/lh); }
    int col(size_t start,size_t end) const {
        int n=0; for (size_t p=start;p<end;p=editor.next(p)) n += editor.text[p]==L'\t' ? 8-n%8 : 1; return n;
    }
    int rowOf(size_t p) const {
        for (size_t i=0;i<rows.size();++i) if (p<rows[i].end || (p==rows[i].end && (i+1==rows.size() || rows[i+1].start!=p))) return static_cast<int>(i);
        return static_cast<int>(rows.size())-1;
    }
    int maxColumns() const { int n=0; for(auto r:rows) n=std::max(n,col(r.start,r.end)); return n; }
    size_t atColumn(int row,int column) const {
        auto r=rows[std::clamp(row,0,static_cast<int>(rows.size())-1)]; int c=0;
        for(size_t p=r.start;p<r.end;p=editor.next(p)) { int n=editor.text[p]==L'\t'?8-c%8:1; if(column<c+(n+1)/2) return p; c+=n; }
        return r.end;
    }
    void layout() {
        rows=editor.rows(columns(),wrap);
        top=std::clamp(top,0,std::max(0,static_cast<int>(rows.size())-page()));
        left=wrap?0:std::clamp(left,0,std::max(0,maxColumns()-columns()+1));
    }
    void reveal() {
        layout(); int row=rowOf(editor.caret), c=col(rows[row].start,editor.caret);
        if(row<top) top=row;
        if(row>=top+page()) top=row-page()+1;
        if(!wrap) { if(c<left) left=c; if(c>=left+columns()) left=c-columns()+1; }
        blink=true; invalidate();
    }
    void edited() { preferredColumn=-1; reveal(); updateTitle(); }
    void updateTitle() {
        auto name=path.empty()?L"Untitled":std::filesystem::path(path).filename().wstring();
        auto title=name+(editor.dirty()?L" *":L"")+L" - RetroNP";
        if(title!=windowTitle) { SetWindowTextW(hwnd,title.c_str()); windowTitle=title; }
    }
    void fonts() {
        // These Windows bitmap faces need not be registered on modern Windows.
        // Load privately so "MS Sans Serif" cannot silently become Arial.
        if(privateFonts.empty()) {
            wchar_t windows[MAX_PATH]{}; GetWindowsDirectoryW(windows,MAX_PATH);
            for(auto file:{L"vgafix.fon",L"vgasys.fon",L"sserife.fon"}) {
                auto path=(std::filesystem::path(windows)/L"Fonts"/file).wstring();
                if(AddFontResourceExW(path.c_str(),FR_PRIVATE,nullptr)) privateFonts.push_back(path);
            }
        }
        if(uiFont) DeleteObject(uiFont);
        if(titleFont) DeleteObject(titleFont);
        if(textFont) DeleteObject(textFont);
        uiFont=CreateFontW(13,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,ANSI_CHARSET,OUT_RASTER_PRECIS,CLIP_DEFAULT_PRECIS,NONANTIALIASED_QUALITY,DEFAULT_PITCH,L"MS Sans Serif");
        titleFont=CreateFontW(16,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,ANSI_CHARSET,OUT_RASTER_PRECIS,CLIP_DEFAULT_PRECIS,NONANTIALIASED_QUALITY,DEFAULT_PITCH,L"System");
        textFont=CreateFontW(16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,ANSI_CHARSET,OUT_RASTER_PRECIS,CLIP_DEFAULT_PRECIS,NONANTIALIASED_QUALITY,FIXED_PITCH|FF_MODERN,L"Fixedsys");
        HDC dc=CreateCompatibleDC(nullptr); auto old=SelectObject(dc,textFont); TEXTMETRICW tm{}; GetTextMetricsW(dc,&tm); cw=tm.tmAveCharWidth; lh=std::max(16L,tm.tmHeight); SelectObject(dc,old); DeleteDC(dc);
    }
    static void fill(HDC dc,RECT r,COLORREF color) { HBRUSH b=CreateSolidBrush(color); FillRect(dc,&r,b); DeleteObject(b); }
    static void line(HDC dc,int x1,int y1,int x2,int y2,COLORREF color) { HPEN p=CreatePen(PS_SOLID,1,color); auto old=SelectObject(dc,p); MoveToEx(dc,x1,y1,nullptr); LineTo(dc,x2,y2); SelectObject(dc,old); DeleteObject(p); }
    static void bevel(HDC dc,RECT r,bool down=false) {
        fill(dc,r,face);
        line(dc,r.left,r.bottom-1,r.left,r.top,down?dark:light); line(dc,r.left,r.top,r.right-1,r.top,down?dark:light);
        line(dc,r.right-1,r.top,r.right-1,r.bottom,down?light:dark); line(dc,r.right-1,r.bottom-1,r.left,r.bottom-1,down?light:dark);
        InflateRect(&r,-1,-1);
        line(dc,r.left,r.bottom-1,r.left,r.top,down?shadow:face); line(dc,r.left,r.top,r.right-1,r.top,down?shadow:face);
        line(dc,r.right-1,r.top,r.right-1,r.bottom,down?face:shadow); line(dc,r.right-1,r.bottom-1,r.left,r.bottom-1,down?face:shadow);
    }
    void text(HDC dc,const std::wstring& s,RECT r,COLORREF color=dark,UINT flags=DT_LEFT|DT_SINGLELINE|DT_VCENTER) const { SetTextColor(dc,color); SetBkMode(dc,TRANSPARENT); DrawTextW(dc,s.c_str(),static_cast<int>(s.size()),&r,flags); }
    static bool inside(RECT r,int x,int y) { return x>=r.left && x<r.right && y>=r.top && y<r.bottom; }
    RECT titleButton(int i) const { int x=width-22-(2-i)*16-(i<2?2:0); return {x,6,x+16,20}; }
    void icon(HDC dc,int x,int y) {
        for(int row=0;row<16;++row) for(int col=0;col<16;++col) {
            char p=notepadPixels[row][col]; if(p!=' ') fill(dc,{x+col,y+row,x+col+1,y+row+1},p=='W'?light:p=='G'?shadow:dark);
        }
    }
    void title(HDC dc,RECT r,const std::wstring& name,bool active) {
        fill(dc,r,active?blue:shadow); SelectObject(dc,titleFont); r.left+=5; text(dc,name,r,light,DT_LEFT|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
    }
    void button(HDC dc,RECT r,const std::wstring& label,bool pressed=false,bool focus=false) {
        bevel(dc,r,pressed); if(pressed) OffsetRect(&r,1,1); SelectObject(dc,uiFont); text(dc,label,r,dark,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        if(focus) { InflateRect(&r,-4,-4); DrawFocusRect(dc,&r); }
    }
    void drawTitleButtons(HDC dc) {
        for(int i=0;i<3;++i) {
            RECT b=titleButton(i); bool pressed=titlePress==i; bevel(dc,b,pressed); if(pressed) OffsetRect(&b,1,1);
            int x=b.left+3,y=b.top+3;
            if(i==0) fill(dc,{x+1,y+6,x+7,y+8},dark);
            else if(i==1) {
                auto box=[&](int xx,int yy,int w,int h) { fill(dc,{xx,yy,xx+w,yy+h},dark); fill(dc,{xx+1,yy+2,xx+w-1,yy+h-1},face); };
                if(IsZoomed(hwnd)) { box(x+2,y-1,7,7); box(x,y+1,7,7); }
                else box(x,y-1,9,9);
            } else {
                constexpr BYTE glyph[]={0xC3,0x66,0x3C,0x18,0x3C,0x66,0xC3};
                for(int yy=0;yy<7;++yy) for(int xx=0;xx<8;++xx) if(glyph[yy]&(0x80>>xx)) fill(dc,{x+xx+1,y+yy,x+xx+2,y+yy+1},dark);
            }
        }
    }
    Scroll makeScroll(RECT r,bool vert,int pos,int max,int visible,int total) {
        Scroll s; s.max=max; s.page=visible; s.before=r; s.after=r;
        if(vert) { s.before.bottom=r.top+16; s.after.top=r.bottom-16; r.top+=16; r.bottom-=16; }
        else { s.before.right=r.left+16; s.after.left=r.right-16; r.left+=16; r.right-=16; }
        s.track=r; int length=vert?r.bottom-r.top:r.right-r.left;
        int thumb=max?std::max(16,length*visible/std::max(1,total)):length;
        thumb=std::min(length,thumb); int offset=max?(length-thumb)*pos/max:0; s.thumb=r;
        if(vert) { s.thumb.top+=offset; s.thumb.bottom=s.thumb.top+thumb; } else { s.thumb.left+=offset; s.thumb.right=s.thumb.left+thumb; }
        return s;
    }
    void drawScroll(HDC dc,Scroll& s,bool vert) {
        fill(dc,s.track,light);
        // Checkerboard scrollbar trough, as on Windows 95.
        const WORD bits[8]={0x55,0xAA,0x55,0xAA,0x55,0xAA,0x55,0xAA};
        HBITMAP pattern=CreateBitmap(8,8,1,1,bits); HBRUSH brush=CreatePatternBrush(pattern);
        SetTextColor(dc,face); SetBkColor(dc,light); FillRect(dc,&s.track,brush); DeleteObject(brush); DeleteObject(pattern);
        if(s.max>0) bevel(dc,s.thumb);
        for(int i=0;i<2;++i) {
            RECT r=i?s.after:s.before; bevel(dc,r); int x=(r.left+r.right)/2,y=(r.top+r.bottom)/2;
            COLORREF c=s.max>0?dark:shadow;
            for(int k=0;k<4;++k) { if(vert) { int yy=y+(i?2-k:k-2); line(dc,x-k,yy,x+k+1,yy,c); } else { int xx=x+(i?2-k:k-2); line(dc,xx,y-k,xx,y+k+1,c); } }
        }
    }
    void drawEditor(HDC dc) {
        RECT r=editorRect(); RECT border={r.left-2,r.top-2,r.right+18,r.bottom+18}; bevel(dc,border,true); fill(dc,r,light);
        int clip=SaveDC(dc); IntersectClipRect(dc,r.left,r.top,r.right,r.bottom); SelectObject(dc,textFont); SetBkMode(dc,OPAQUE);
        for(int row=top;row<static_cast<int>(rows.size()) && row<=top+page();++row) {
            auto segment=rows[row]; int x=r.left+3-left*cw,y=r.top+2+(row-top)*lh,c=0;
            for(size_t p=segment.start;p<segment.end;) {
                size_t next=editor.next(p); int n=editor.text[p]==L'\t'?8-c%8:1;
                RECT cell={x+c*cw,y,x+(c+n)*cw,y+lh}; bool selected=p>=editor.lo() && p<editor.hi();
                if(cell.right>=r.left && cell.left<r.right) {
                    SetTextColor(dc,selected?light:dark); SetBkColor(dc,selected?blue:light);
                    if(editor.text[p]==L'\t') ExtTextOutW(dc,cell.left,y,ETO_OPAQUE,&cell,L"",0,nullptr);
                    else ExtTextOutW(dc,cell.left,y,ETO_OPAQUE|ETO_CLIPPED,&cell,editor.text.c_str()+p,static_cast<UINT>(next-p),nullptr);
                }
                c+=n; p=next;
            }
            if(segment.end<editor.text.size() && editor.text[segment.end]==L'\n' && segment.end>=editor.lo() && segment.end<editor.hi()) fill(dc,{x+c*cw,y,x+(c+1)*cw,y+lh},blue);
        }
        RestoreDC(dc,clip);
        vertical=makeScroll({r.right,r.top,r.right+16,r.bottom},true,top,std::max(0,static_cast<int>(rows.size())-page()),page(),static_cast<int>(rows.size()));
        horizontal=makeScroll({r.left,r.bottom,r.right,r.bottom+16},false,left,wrap?0:std::max(0,maxColumns()-columns()+1),columns(),maxColumns()+1);
        drawScroll(dc,vertical,true); drawScroll(dc,horizontal,false); fill(dc,{r.right,r.bottom,r.right+16,r.bottom+16},face);
        for(int k=0;k<3;++k) { int t=k*4; line(dc,r.right+3+t,r.bottom+14,r.right+14,r.bottom+3+t,light); line(dc,r.right+4+t,r.bottom+14,r.right+14,r.bottom+4+t,shadow); }
    }
    RECT menuRect() const {
        const auto& m=menus[activeMenu]; int h=6; for(auto& i:m.items) h+=i.command?19:9;
        return {m.x,43,m.x+232,43+h};
    }
    bool enabled(int command) const {
        if(command==Undo) return !editor.undoStack.empty();
        if(command==Redo) return !editor.redoStack.empty();
        if(command==Copy || command==Cut || command==Delete) return editor.selected();
        if(command==Paste) return IsClipboardFormatAvailable(CF_UNICODETEXT)!=0;
        return true;
    }
    void drawMenu(HDC dc) {
        if(activeMenu<0) return;
        RECT r=menuRect(); bevel(dc,r); SelectObject(dc,uiFont);
        int y=r.top+3,index=0;
        for(const auto& i:menus[activeMenu].items) {
            if(!i.command) { line(dc,r.left+3,y+3,r.right-3,y+3,shadow); line(dc,r.left+3,y+4,r.right-3,y+4,light); y+=9; ++index; continue; }
            RECT item={r.left+3,y,r.right-3,y+19}; bool hot=hover==index; bool on=enabled(i.command); if(hot) fill(dc,item,blue);
            COLORREF color=hot?light:(on?dark:shadow); RECT label=item; label.left+=23;
            RECT key=item; key.right-=12;
            if(!on && !hot) { RECT embossedLabel=label,embossedKey=key; OffsetRect(&embossedLabel,1,1); OffsetRect(&embossedKey,1,1); text(dc,i.label,embossedLabel,light); text(dc,i.shortcut,embossedKey,light,DT_RIGHT|DT_SINGLELINE|DT_VCENTER); }
            text(dc,i.label,label,on?color:shadow); text(dc,i.shortcut,key,on?color:shadow,DT_RIGHT|DT_SINGLELINE|DT_VCENTER);
            if(i.command==Wrap && wrap) { int x=item.left+7; line(dc,x,y+12,x+3,y+15,color); line(dc,x+3,y+15,x+9,y+7,color); }
            y+=19; ++index;
        }
    }
    RECT dialogRect() const { int w=std::min(560,width-24),h=(dialog==Dialog::Confirm?152:dialog==Dialog::About?214:dialog==Dialog::Message?190:168); return {(width-w)/2,(height-h)/2,(width+w)/2,(height+h)/2}; }
    RECT inputRect() const { auto r=dialogRect(); return {r.left+16,r.top+68,r.right-16,r.top+93}; }
    std::vector<std::wstring> dialogButtons() const { if(dialog==Dialog::Confirm) return {L"&Yes",L"&No",L"Cancel"}; if(dialog==Dialog::Message || dialog==Dialog::About) return {L"OK"}; return {dialog==Dialog::Find?L"Find &Next":dialog==Dialog::Open?L"&Open":L"&Save",L"Cancel"}; }
    RECT dialogButton(int i) const { auto r=dialogRect(); int count=static_cast<int>(dialogButtons().size()); int x=r.right-16-(count-i)*88; return {x,r.bottom-39,x+80,r.bottom-15}; }
    void drawDialog(HDC dc) {
        if(dialog==Dialog::None) return;
        if(showOverwrite) { drawOverwrite(dc); return; }
        RECT r=dialogRect(); bevel(dc,r);
        std::wstring name=dialog==Dialog::Open?L"Open":dialog==Dialog::Save?L"Save As":dialog==Dialog::Find?L"Find":dialog==Dialog::Confirm?L"RetroNP":dialog==Dialog::About?L"About RetroNP":messageTitle;
        title(dc,{r.left+3,r.top+3,r.right-3,r.top+25},name,true); SelectObject(dc,uiFont);
        bool field=dialog==Dialog::Open || dialog==Dialog::Save || dialog==Dialog::Find;
        if(field) {
            text(dc,dialog==Dialog::Find?L"Fi&nd what:":L"File &name (full path or relative to the working folder):",{r.left+16,r.top+36,r.right-16,r.top+61});
            RECT box=inputRect(); bevel(dc,box,true); InflateRect(&box,-3,-3); fill(dc,box,light);
            int old=SaveDC(dc); IntersectClipRect(dc,box.left,box.top,box.right,box.bottom); SelectObject(dc,textFont);
            int visible=std::max(1,static_cast<int>((box.right-box.left)/cw)); int offset=std::max(0,static_cast<int>(input.caret)-visible+1);
            for(size_t p=static_cast<size_t>(offset);p<input.text.size();++p) {
                RECT c={box.left+(static_cast<int>(p)-offset)*cw,box.top,box.left+(static_cast<int>(p)-offset+1)*cw,box.bottom}; bool sel=p>=input.lo() && p<input.hi();
                SetBkColor(dc,sel?blue:light); SetTextColor(dc,sel?light:dark); ExtTextOutW(dc,c.left,c.top,ETO_OPAQUE|ETO_CLIPPED,&c,input.text.c_str()+p,1,nullptr);
            }
            RestoreDC(dc,old); SelectObject(dc,uiFont);
            text(dc,dialog==Dialog::Find?L"Search ignores case and wraps to the beginning.":L"Tip: you can also drag a text file onto RetroNP.",{r.left+16,r.top+96,r.right-16,r.top+116},shadow);
        } else if(dialog==Dialog::Confirm) {
            std::wstring name=path.empty()?L"Untitled":std::filesystem::path(path).filename().wstring();
            text(dc,L"Save changes to \""+name+L"\"?",{r.left+17,r.top+40,r.right-17,r.bottom-47},dark,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);
        } else if(dialog==Dialog::About) {
            icon(dc,r.left+20,r.top+46); SelectObject(dc,titleFont); text(dc,L"RetroNP",{r.left+49,r.top+38,r.right-16,r.top+66}); SelectObject(dc,uiFont);
            text(dc,L"A little piece of 1995, written in C++.\n\nEvery control is drawn by hand with Win32 GDI.\nNo UI frameworks. Just pixels and plain text.\n\nVersion 1.0",{r.left+20,r.top+76,r.right-20,r.bottom-46},dark,DT_LEFT|DT_WORDBREAK);
        } else text(dc,message,{r.left+17,r.top+40,r.right-17,r.bottom-47},dark,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);
        auto buttons=dialogButtons(); for(size_t i=0;i<buttons.size();++i) button(dc,dialogButton(static_cast<int>(i)),buttons[i],false,dialogFocus==static_cast<int>(i)+1);
    }
    void drawCanvas(HDC dc) {
        bevel(dc,{0,0,width,height}); line(dc,2,2,width-2,2,face);
        auto name=path.empty()?L"Untitled":std::filesystem::path(path).filename().wstring();
        title(dc,{4,4,width-4,23},L"",focused && dialog==Dialog::None); icon(dc,6,5); SelectObject(dc,titleFont);
        text(dc,name+(editor.dirty()?L" *":L"")+L" - RetroNP",{25,4,width-61,23},light,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        drawTitleButtons(dc); SelectObject(dc,uiFont);
        for(size_t i=0;i<menus.size();++i) { auto& m=menus[i]; RECT r={m.x,24,m.x+m.width,43}; bool active=activeMenu==static_cast<int>(i); if(active) fill(dc,r,blue); text(dc,m.label,r,active?light:dark,DT_CENTER|DT_VCENTER|DT_SINGLELINE); }
        layout(); drawEditor(dc); drawMenu(dc); drawDialog(dc);
    }
    void invalidate() {
        canvasDirty=true;
        if(hwnd) InvalidateRect(hwnd,nullptr,FALSE);
    }
    void invalidate(RECT logical) {
        canvasDirty=true;
        if(hwnd) { RECT client{}; GetClientRect(hwnd,&client); RECT rect=deviceRect(logical,client); InvalidateRect(hwnd,&rect,FALSE); }
    }
    RECT caretBounds() const {
        RECT caret{},clip{};
        if(!focused || activeMenu>=0 || showOverwrite) return caret;
        if(dialog==Dialog::None) {
            if(rows.empty()) return caret;
            clip=editorRect(); int row=rowOf(editor.caret);
            int x=clip.left+3+(col(rows[row].start,editor.caret)-left)*cw,y=clip.top+2+(row-top)*lh;
            caret={x,y,x+1,y+lh};
        } else if(dialogFocus==0 && (dialog==Dialog::Open || dialog==Dialog::Save || dialog==Dialog::Find)) {
            clip=inputRect(); InflateRect(&clip,-3,-3);
            int visible=std::max<int>(1,(clip.right-clip.left)/cw);
            int offset=std::max(0,static_cast<int>(input.caret)-visible+1);
            int x=clip.left+(static_cast<int>(input.caret)-offset)*cw;
            caret={x,clip.top,x+1,clip.top+lh};
        }
        RECT result{}; IntersectRect(&result,&caret,&clip); return result;
    }
    RECT deviceRect(RECT logical,RECT client) const {
        // Round outward to cover every destination pixel of the logical caret.
        int w=std::max(1,width),h=std::max(1,height);
        return {logical.left*client.right/w,logical.top*client.bottom/h,
            (logical.right*client.right+w-1)/w,(logical.bottom*client.bottom+h-1)/h};
    }
    void blinkCaret() {
        RECT caret=caretBounds();
        if(IsRectEmpty(&caret)) return;
        blink=!blink;
        RECT client{}; GetClientRect(hwnd,&client); caret=deviceRect(caret,client);
        // No document reflow, resource allocation, or full-window invalidation.
        InvalidateRect(hwnd,&caret,FALSE);
    }
    void releaseCanvas() {
        if(canvas && originalBitmap) SelectObject(canvas,originalBitmap);
        if(canvasBitmap) DeleteObject(canvasBitmap);
        if(canvas) DeleteDC(canvas);
        canvas=nullptr; canvasBitmap=nullptr; originalBitmap=nullptr;
        canvasWidth=canvasHeight=0;
    }
    bool present(HDC target,RECT client) {
        if(width<=0 || height<=0 || client.right<=0 || client.bottom<=0) return false;
        if(!canvas || canvasWidth!=width || canvasHeight!=height) {
            releaseCanvas();
            canvas=CreateCompatibleDC(target); if(!canvas) return false;
            canvasBitmap=pixelBitmap(target,width,height);
            if(!canvasBitmap) { releaseCanvas(); return false; }
            originalBitmap=SelectObject(canvas,canvasBitmap); canvasWidth=width; canvasHeight=height; canvasDirty=true;
        }
        if(canvasDirty) {
            int savedState=SaveDC(canvas); drawCanvas(canvas); RestoreDC(canvas,savedState); canvasDirty=false;
        }
        // Complete scaling and caret composition offscreen. The window DC sees
        // exactly one finished, device-sized image, never separate drawing steps.
        if(!frame.ensure(target,client.right,client.bottom)) return false;
        SetStretchBltMode(frame.dc,COLORONCOLOR);
        StretchBlt(frame.dc,0,0,client.right,client.bottom,canvas,0,0,width,height,SRCCOPY);
        RECT caret=caretBounds(); if(blink && !IsRectEmpty(&caret)) fill(frame.dc,deviceRect(caret,client),dark);
        GdiFlush();
        // BeginPaint's clip preserves partial updates (caret/menu highlights).
        BitBlt(target,0,0,client.right,client.bottom,frame.dc,0,0,SRCCOPY);
        GdiFlush();
        return true;
    }
    void paint() {
        PAINTSTRUCT ps{}; HDC target=BeginPaint(hwnd,&ps); RECT client{}; GetClientRect(hwnd,&client);
        present(target,client); EndPaint(hwnd,&ps);
    }
    void show(Dialog kind,const std::wstring& value=L"") { dialog=kind; activeMenu=-1; hover=-1; dialogFocus=0; input.reset(value); input.anchor=0; input.caret=input.text.size(); blink=true; ReleaseCapture(); selecting=false; invalidate(); }
    void info(const std::wstring& body,const std::wstring& title=L"RetroNP") { message=body; messageTitle=title; show(Dialog::Message); dialogFocus=1; }
    static std::wstring systemError() { wchar_t* buffer=nullptr; DWORD code=GetLastError(); FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,nullptr,code,0,reinterpret_cast<wchar_t*>(&buffer),0,nullptr); std::wstring s=buffer?buffer:L"The operation could not be completed."; if(buffer) LocalFree(buffer); return s; }
    static std::string utf8(const std::wstring& s) { int n=WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr); std::string out(n,'\0'); if(n) WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out.data(),n,nullptr,nullptr); return out; }
    static std::wstring decode(const std::string& s,UINT codepage,DWORD flags) { int n=MultiByteToWideChar(codepage,flags,s.data(),static_cast<int>(s.size()),nullptr,0); if(n==0 && !s.empty()) throw std::runtime_error("Invalid text encoding"); std::wstring out(n,L'\0'); if(n) MultiByteToWideChar(codepage,flags,s.data(),static_cast<int>(s.size()),out.data(),n); return out; }
    void load(const std::wstring& filename) {
        try {
            auto resolved=std::filesystem::absolute(filename);
            if(std::filesystem::file_size(resolved)>16*1024*1024) { info(L"This file is too large. RetroNP supports text files up to 16 MB.",L"Open"); return; }
            std::ifstream file(resolved,std::ios::binary); if(!file) throw std::runtime_error("open failed");
            std::string bytes((std::istreambuf_iterator<char>(file)),{}); if(file.bad()) throw std::runtime_error("read failed");
            bool is16=bytes.size()>=2 && static_cast<unsigned char>(bytes[0])==0xFF && static_cast<unsigned char>(bytes[1])==0xFE;
            bool big16=bytes.size()>=2 && static_cast<unsigned char>(bytes[0])==0xFE && static_cast<unsigned char>(bytes[1])==0xFF;
            bool bom=bytes.size()>=3 && bytes.compare(0,3,"\xEF\xBB\xBF")==0; std::wstring content;
            if(is16 || big16) {
                if((bytes.size()-2)%2) throw std::runtime_error("Invalid UTF-16");
                for(size_t i=2;i<bytes.size();i+=2) { auto a=static_cast<unsigned char>(bytes[i]),b=static_cast<unsigned char>(bytes[i+1]); content+=static_cast<wchar_t>(big16?(a<<8)|b:a|(b<<8)); }
            } else {
                if(bom) bytes.erase(0,3);
                try { content=decode(bytes,CP_UTF8,MB_ERR_INVALID_CHARS); } catch(...) { if(bom) throw; content=decode(bytes,CP_ACP,0); }
            }
            if(content.find(L'\0')!=std::wstring::npos) { info(L"This appears to be a binary file. Please open a plain text file.",L"Open"); return; }
            editor.reset(content); path=resolved.wstring(); utf16=is16||big16; utf8bom=bom; top=left=0; updateTitle(); reveal();
        } catch(...) { info(L"Could not read the file. Check the path, file permissions, and text encoding.",L"Open"); }
    }
    bool write(const std::wstring& filename) {
        std::filesystem::path temp;
        try {
            auto resolved=std::filesystem::absolute(filename); temp=resolved;
            temp+=L".retronp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".tmp";
            std::wstring content; for(auto c:editor.text) { if(c==L'\n') content+=L'\r'; content+=c; }
            std::string bytes;
            if(utf16) { bytes="\xFF\xFE"; for(wchar_t c:content) { bytes+=static_cast<char>(c&255); bytes+=static_cast<char>((c>>8)&255); } }
            else { bytes=utf8bom?"\xEF\xBB\xBF":""; bytes+=utf8(content); }
            HANDLE file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(file==INVALID_HANDLE_VALUE) { info(L"Could not create a temporary file.\n"+systemError(),L"Save"); return false; }
            DWORD count=0; bool ok=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr)!=0 && count==bytes.size();
            if(ok) ok=FlushFileBuffers(file)!=0;
            CloseHandle(file);
            if(!ok || !MoveFileExW(temp.c_str(),resolved.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) { auto error=systemError(); DeleteFileW(temp.c_str()); info(L"Could not save the file.\n"+error,L"Save"); return false; }
            path=resolved.wstring(); editor.saved=editor.text; updateTitle(); return true;
        } catch(...) { if(!temp.empty()) DeleteFileW(temp.c_str()); info(L"Could not save the file. Check the file name and folder permissions.",L"Save"); return false; }
    }
    void continuePending() {
        Pending action=pending; pending=Pending::None;
        if(action==Pending::Exit) DestroyWindow(hwnd);
        else if(action==Pending::New) { editor.reset(); path.clear(); utf16=utf8bom=false; top=left=0; updateTitle(); reveal(); }
        else if(action==Pending::Open) show(Dialog::Open);
        else if(action==Pending::Drop) load(droppedPath);
    }
    void request(Pending action) { pending=action; if(editor.dirty()) { show(Dialog::Confirm); dialogFocus=1; } else continuePending(); }
    void save(bool as=false) {
        if(path.empty() || as) { show(Dialog::Save,path.empty()?L"Untitled.txt":path); return; }
        if(write(path)) continuePending(); else pending=Pending::None;
    }
    void copy(Editor& e) {
        if(!e.selected() || !OpenClipboard(hwnd)) return;
        EmptyClipboard();
        std::wstring s; for(auto c:e.selection()) { if(c==L'\n') s+=L'\r'; s+=c; }
        HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,(s.size()+1)*sizeof(wchar_t));
        if(memory) { void* data=GlobalLock(memory); if(data) { memcpy(data,s.c_str(),(s.size()+1)*sizeof(wchar_t)); GlobalUnlock(memory); if(!SetClipboardData(CF_UNICODETEXT,memory)) GlobalFree(memory); } else GlobalFree(memory); }
        CloseClipboard();
    }
    void paste(Editor& e,bool single=false) {
        if(!OpenClipboard(hwnd)) return;
        HANDLE memory=GetClipboardData(CF_UNICODETEXT);
        if(memory) { const wchar_t* data=static_cast<const wchar_t*>(GlobalLock(memory)); if(data) { std::wstring s=data; GlobalUnlock(memory); if(single) for(auto& c:s) if(c==L'\r'||c==L'\n'||c==L'\t') c=L' '; e.replace(s); } } CloseClipboard();
    }
    void findNext() { if(query.empty()) show(Dialog::Find); else if(!editor.find(query,false)) info(L"Cannot find \""+query+L"\".",L"Find"); else reveal(); }
    void execute(int command) {
        activeMenu=-1; hover=-1; ReleaseCapture(); invalidate();
        switch(command) {
        case New: request(Pending::New); break; case Open: request(Pending::Open); break; case Save: save(); break; case SaveAs: save(true); break; case Exit: request(Pending::Exit); break;
        case Undo: editor.undo(); edited(); break; case Redo: editor.redo(); edited(); break;
        case Copy: copy(editor); break; case Cut: copy(editor); editor.replace(L""); edited(); break; case Paste: paste(editor); edited(); break;
        case Delete: editor.del(); edited(); break; case SelectAll: editor.anchor=0; editor.caret=editor.text.size(); reveal(); break;
        case Wrap: wrap=!wrap; left=0; preferredColumn=-1; reveal(); break;
        case Find: show(Dialog::Find,editor.selected()?editor.selection():query); break; case FindNext: findNext(); break;
        case About: show(Dialog::About); dialogFocus=1; break;
        case TimeDate: {
            SYSTEMTIME now{}; GetLocalTime(&now); wchar_t time[80]{}, date[80]{}; GetTimeFormatW(LOCALE_USER_DEFAULT,TIME_NOSECONDS,&now,nullptr,time,80); GetDateFormatW(LOCALE_USER_DEFAULT,DATE_SHORTDATE,&now,nullptr,date,80);
            editor.replace(std::wstring(time)+L" "+date); edited(); break;
        }
        }
    }
    void cancelDialog() { dialog=Dialog::None; pending=Pending::None; inputSelecting=false; showOverwrite=false; invalidate(); }
    void acceptDialog(int button) {
        if(dialog==Dialog::Confirm) {
            if(button==0) { dialog=Dialog::None; save(); } else if(button==1) { dialog=Dialog::None; continuePending(); } else cancelDialog();
        } else if(dialog==Dialog::Message || dialog==Dialog::About) cancelDialog();
        else if(button!=0) cancelDialog();
        else if(dialog==Dialog::Find) { query=input.text; if(!query.empty()) { dialog=Dialog::None; findNext(); } }
        else {
            std::wstring filename=input.text;
            if(filename.size()>=2 && filename.front()==L'"' && filename.back()==L'"') filename=filename.substr(1,filename.size()-2);
            if(filename.empty()) return;
            Dialog kind=dialog; dialog=Dialog::None;
            if(kind==Dialog::Open) load(filename);
            else {
                // An existing target different from the current document needs explicit overwrite consent.
                std::error_code ec; bool exists=std::filesystem::exists(filename,ec); bool same=false;
                if(!path.empty()) same=std::filesystem::equivalent(filename,path,ec);
                if(exists && !same) { overwritePath=filename; message=L"\""+std::filesystem::path(filename).filename().wstring()+L"\" already exists. Replace it?"; showOverwrite=true; show(Dialog::Message); messageTitle=L"Confirm Save As"; dialogFocus=2; }
                else if(write(filename)) continuePending(); else pending=Pending::None;
            }
        }
        invalidate();
    }
    std::wstring overwritePath; bool showOverwrite=false;
    void overwriteAnswer(bool yes) { showOverwrite=false; dialog=Dialog::None; if(yes) { if(write(overwritePath)) continuePending(); else pending=Pending::None; } else show(Dialog::Save,overwritePath); invalidate(); }
    void keyEditor(Editor& e,WPARAM key,bool ctrl,bool shift,bool single=false) {
        if(ctrl) {
            if(key=='A') { e.anchor=0; e.caret=e.text.size(); }
            else if(key=='C') copy(e); else if(key=='X') { copy(e); if(e.selected()) e.replace(L""); }
            else if(key=='V') paste(e,single); else if(key=='Z') { if(shift) e.redo(); else e.undo(); } else if(key=='Y') e.redo();
            else if(key==VK_LEFT) e.move(e.word(e.caret,-1),shift); else if(key==VK_RIGHT) e.move(e.word(e.caret,1),shift);
            else if(key==VK_HOME) e.move(0,shift); else if(key==VK_END) e.move(e.text.size(),shift);
            else if(key==VK_BACK) { if(!e.selected()) e.anchor=e.word(e.caret,-1); e.backspace(); }
            else if(key==VK_DELETE) { if(!e.selected()) e.anchor=e.word(e.caret,1); e.del(); }
        } else {
            if(key==VK_LEFT) e.move(e.selected()&&!shift?e.lo():e.previous(e.caret),shift);
            else if(key==VK_RIGHT) e.move(e.selected()&&!shift?e.hi():e.next(e.caret),shift);
            else if(key==VK_BACK) e.backspace(); else if(key==VK_DELETE) e.del();
            else if(key==VK_HOME) e.move(e.lineStart(e.caret),shift); else if(key==VK_END) e.move(e.lineEnd(e.caret),shift);
        }
    }
    void key(WPARAM vk) {
        bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0, shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
        if(dialog!=Dialog::None) {
            if(vk==VK_RETURN || ((dialog==Dialog::Confirm || showOverwrite) && (vk=='Y' || vk=='N'))) eatCharacter=true;
            if(showOverwrite) { if(vk=='Y') overwriteAnswer(true); else if(vk=='N'||vk==VK_ESCAPE) overwriteAnswer(false); else if(vk==VK_TAB||vk==VK_LEFT||vk==VK_RIGHT) { dialogFocus=dialogFocus==1?2:1; invalidate(); } else if(vk==VK_RETURN) overwriteAnswer(dialogFocus==1); return; }
            bool field=dialog==Dialog::Open||dialog==Dialog::Save||dialog==Dialog::Find;
            if(vk==VK_ESCAPE) cancelDialog(); else if(vk==VK_RETURN) acceptDialog(std::max(0,dialogFocus-1));
            else if(vk==VK_TAB) { int count=static_cast<int>(dialogButtons().size())+(field?1:0),base=field?0:1; dialogFocus=base+(dialogFocus-base+(shift?count-1:1))%count; }
            else if(dialog==Dialog::Confirm && (vk=='Y'||vk=='N')) acceptDialog(vk=='Y'?0:1);
            else if(dialogFocus==0 && field) keyEditor(input,vk,ctrl,shift,true);
            else if(vk==VK_LEFT || vk==VK_RIGHT) { int count=static_cast<int>(dialogButtons().size()); dialogFocus=1+(dialogFocus-1+(vk==VK_LEFT?count-1:1))%count; }
            blink=true; invalidate(); return;
        }
        if(activeMenu>=0) {
            if(vk==VK_RETURN || (vk>=32 && vk<=126)) eatCharacter=true;
            if(vk==VK_ESCAPE) { activeMenu=-1; ReleaseCapture(); }
            else if(vk==VK_LEFT || vk==VK_RIGHT) { activeMenu=(activeMenu+(vk==VK_LEFT?3:1))%4; hover=-1; }
            else if(vk==VK_DOWN || vk==VK_UP) {
                auto& items=menus[activeMenu].items; int n=static_cast<int>(items.size());
                do { hover=(hover+(vk==VK_DOWN?1:n-1)+n)%n; } while(!items[hover].command);
            } else if(vk==VK_RETURN && hover>=0) { int c=menus[activeMenu].items[hover].command; if(enabled(c)) execute(c); }
            else for(const auto& i:menus[activeMenu].items) { auto p=i.label.find(L'&'); if(p!=std::wstring::npos && towupper(i.label[p+1])==static_cast<wint_t>(vk)) { if(enabled(i.command)) execute(i.command); break; } }
            invalidate(); return;
        }
        if(ctrl) {
            if(vk=='N') { execute(New); return; } if(vk=='O') { execute(Open); return; } if(vk=='S') { execute(shift?SaveAs:Save); return; } if(vk=='F') { execute(Find); return; }
        }
        if(vk==VK_F3) { execute(FindNext); return; } if(vk==VK_F5) { execute(TimeDate); return; }
        if(vk==VK_F10) { activeMenu=0; hover=-1; invalidate(); return; }
        // Printable keys, Enter, and Tab are edited by WM_CHAR. Do not paint an
        // extra unchanged frame on WM_KEYDOWN (or on modifier-only key presses).
        bool navigation=vk==VK_LEFT || vk==VK_RIGHT || vk==VK_UP || vk==VK_DOWN || vk==VK_HOME || vk==VK_END || vk==VK_PRIOR || vk==VK_NEXT;
        bool editKey=vk==VK_BACK || vk==VK_DELETE || (ctrl && (vk=='A' || vk=='C' || vk=='X' || vk=='V' || vk=='Z' || vk=='Y'));
        if(!navigation && !editKey) return;
        layout();
        if(!ctrl && (vk==VK_UP || vk==VK_DOWN || vk==VK_PRIOR || vk==VK_NEXT)) {
            int row=rowOf(editor.caret); if(preferredColumn<0) preferredColumn=col(rows[row].start,editor.caret);
            int amount=vk==VK_UP?-1:vk==VK_DOWN?1:vk==VK_PRIOR?-page():page();
            editor.move(atColumn(row+amount,preferredColumn),shift); reveal(); return;
        }
        if(!ctrl && (vk==VK_HOME || vk==VK_END)) { auto r=rows[rowOf(editor.caret)]; editor.move(vk==VK_HOME?r.start:r.end,shift); }
        else keyEditor(editor,vk,ctrl,shift);
        edited();
    }
    void character(wchar_t c) {
        if(eatCharacter) { eatCharacter=false; return; }
        if(GetKeyState(VK_CONTROL)&0x8000 || activeMenu>=0) return;
        if(dialog!=Dialog::None) {
            if(showOverwrite) return;
            if(dialogFocus==0 && (dialog==Dialog::Open || dialog==Dialog::Save || dialog==Dialog::Find) && c>=32) { input.replace(std::wstring(1,c)); blink=true; invalidate(); }
        } else if(c>=32 || c==L'\r' || c==L'\t') { editor.replace(c==L'\r'?L"\n":std::wstring(1,c)); edited(); }
    }
    int menuHit(int x,int y) const {
        if(activeMenu<0) return -1;
        RECT r=menuRect(); if(!inside(r,x,y)) return -1;
        int yy=r.top+3; auto& items=menus[activeMenu].items;
        for(size_t i=0;i<items.size();++i) { int h=items[i].command?19:9; if(y>=yy && y<yy+h) return items[i].command?static_cast<int>(i):-1; yy+=h; } return -1;
    }
    size_t hitText(int x,int y) const { auto r=editorRect(); return atColumn(top+(y-r.top-2)/lh,left+std::max<int>(0,(x-r.left-3+cw/2)/cw)); }
    size_t hitInput(int x) const { auto r=inputRect(); int visible=std::max(1,static_cast<int>((r.right-r.left-6)/cw)); int offset=std::max(0,static_cast<int>(input.caret)-visible+1); return static_cast<size_t>(std::clamp<int>(offset+(x-r.left-3+cw/2)/cw,0,static_cast<int>(input.text.size()))); }
    bool scrollClick(Scroll& s,bool vert,int x,int y) {
        int& pos=vert?top:left;
        if(inside(s.before,x,y)) { pos=std::max(0,pos-1); }
        else if(inside(s.after,x,y)) { pos=std::min(s.max,pos+1); }
        else if(inside(s.track,x,y)) {
            if(s.max && inside(s.thumb,x,y)) { scrollDrag=vert?1:2; dragOffset=vert?y-s.thumb.top:x-s.thumb.left; SetCapture(hwnd); }
            else { bool before=vert?y<s.thumb.top:x<s.thumb.left; pos=std::clamp(pos+(before?-s.page:s.page),0,s.max); }
        } else return false;
        invalidate(); return true;
    }
    void mouseDown(int x,int y,bool doubleClick=false) {
        SetFocus(hwnd);
        if(dialog!=Dialog::None) {
            if(showOverwrite) { auto r=dialogRect(); RECT yes={r.right-192,r.bottom-39,r.right-112,r.bottom-15},no={r.right-104,r.bottom-39,r.right-24,r.bottom-15}; if(inside(yes,x,y)) overwriteAnswer(true); else if(inside(no,x,y)) overwriteAnswer(false); return; }
            auto buttons=dialogButtons(); for(size_t i=0;i<buttons.size();++i) if(inside(dialogButton(static_cast<int>(i)),x,y)) { acceptDialog(static_cast<int>(i)); return; }
            if((dialog==Dialog::Open || dialog==Dialog::Save || dialog==Dialog::Find) && inside(inputRect(),x,y)) { dialogFocus=0; input.move(hitInput(x),(GetKeyState(VK_SHIFT)&0x8000)!=0); inputSelecting=true; SetCapture(hwnd); if(doubleClick) { input.anchor=0; input.caret=input.text.size(); } }
            invalidate(); return;
        }
        if(activeMenu>=0 && inside(menuRect(),x,y)) { int i=menuHit(x,y); if(i>=0 && enabled(menus[activeMenu].items[i].command)) execute(menus[activeMenu].items[i].command); return; }
        for(size_t i=0;i<menus.size();++i) { auto& m=menus[i]; if(inside({m.x,24,m.x+m.width,43},x,y)) { activeMenu=activeMenu==static_cast<int>(i)?-1:static_cast<int>(i); hover=-1; invalidate(); return; } }
        if(activeMenu>=0) { activeMenu=-1; invalidate(); return; }
        for(int i=0;i<3;++i) if(inside(titleButton(i),x,y)) { titlePress=i; SetCapture(hwnd); invalidate(); return; }
        if(y>=4 && y<23 && x>4 && x<width-60) { if(doubleClick) ShowWindow(hwnd,IsZoomed(hwnd)?SW_RESTORE:SW_MAXIMIZE); else { ReleaseCapture(); SendMessageW(hwnd,WM_NCLBUTTONDOWN,HTCAPTION,0); } return; }
        if(scrollClick(vertical,true,x,y) || scrollClick(horizontal,false,x,y)) return;
        if(inside(editorRect(),x,y)) {
            editor.move(hitText(x,y),(GetKeyState(VK_SHIFT)&0x8000)!=0); preferredColumn=-1; selecting=true; SetCapture(hwnd);
            if(doubleClick && !editor.text.empty()) { size_t p=std::min(editor.caret,editor.text.size()-1); bool word=iswalnum(editor.text[p]) || editor.text[p]==L'_'; size_t a=p,b=editor.next(p); if(word) { while(a && (iswalnum(editor.text[editor.previous(a)])||editor.text[editor.previous(a)]==L'_')) a=editor.previous(a); while(b<editor.text.size() && (iswalnum(editor.text[b])||editor.text[b]==L'_')) b=editor.next(b); } editor.anchor=a; editor.caret=b; }
            reveal();
        }
    }
    void mouseMove(int x,int y) {
        if(inputSelecting) { input.caret=hitInput(x); invalidate(); return; }
        if(scrollDrag) {
            Scroll& s=scrollDrag==1?vertical:horizontal; bool vert=scrollDrag==1;
            int length=vert?s.track.bottom-s.track.top-(s.thumb.bottom-s.thumb.top):s.track.right-s.track.left-(s.thumb.right-s.thumb.left);
            int at=(vert?y-s.track.top:x-s.track.left)-dragOffset; int value=length>0?std::clamp(at*s.max/length,0,s.max):0;
            (vert?top:left)=value; invalidate(); return;
        }
        if(selecting) {
            auto r=editorRect(); if(y<r.top) top=std::max(0,top-1); else if(y>=r.bottom) top=std::min(vertical.max,top+1);
            if(!wrap) { if(x<r.left) left=std::max(0,left-1); else if(x>=r.right) left=std::min(horizontal.max,left+1); }
            editor.caret=hitText(x,y); blink=true; invalidate(); return;
        }
        if(activeMenu>=0) {
            int previousMenu=activeMenu,previousHover=hover;
            for(size_t i=0;i<menus.size();++i) if(inside({menus[i].x,24,menus[i].x+menus[i].width,43},x,y)) activeMenu=static_cast<int>(i);
            hover=menuHit(x,y);
            if(activeMenu!=previousMenu) invalidate();
            else if(hover!=previousHover) invalidate(menuRect());
        }
    }
    void mouseUp(int x,int y) {
        int pressed=titlePress;
        selecting=false; inputSelecting=false; scrollDrag=0; ReleaseCapture();
        titlePress=-1;
        if(pressed>=0 && inside(titleButton(pressed),x,y)) { if(pressed==0) ShowWindow(hwnd,SW_MINIMIZE); else if(pressed==1) ShowWindow(hwnd,IsZoomed(hwnd)?SW_RESTORE:SW_MAXIMIZE); else request(Pending::Exit); }
        invalidate();
    }
    void drawOverwrite(HDC dc) {
        RECT r=dialogRect(); bevel(dc,r); title(dc,{r.left+3,r.top+3,r.right-3,r.top+25},L"Confirm Save As",true); SelectObject(dc,uiFont);
        text(dc,message,{r.left+17,r.top+40,r.right-17,r.bottom-47},dark,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);
        button(dc,{r.right-192,r.bottom-39,r.right-112,r.bottom-15},L"&Yes",false,dialogFocus==1); button(dc,{r.right-104,r.bottom-39,r.right-24,r.bottom-15},L"&No",false,dialogFocus==2);
    }
    ~App() { releaseCanvas(); if(uiFont) DeleteObject(uiFont); if(titleFont) DeleteObject(titleFont); if(textFont) DeleteObject(textFont); for(auto& file:privateFonts) RemoveFontResourceExW(file.c_str(),FR_PRIVATE,nullptr); }
};

LRESULT CALLBACK windowProc(HWND hwnd,UINT msg,WPARAM w,LPARAM l) {
    auto* app=reinterpret_cast<App*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(msg==WM_NCCREATE) { app=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); app->hwnd=hwnd; SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app)); }
    if(!app) return DefWindowProcW(hwnd,msg,w,l);
    auto point=[&]() { return POINT{MulDiv(GET_X_LPARAM(l),96,app->dpi),MulDiv(GET_Y_LPARAM(l),96,app->dpi)}; };
    switch(msg) {
    case WM_CREATE: app->dpi=static_cast<int>(GetDpiForWindow(hwnd)); app->fonts(); SetTimer(hwnd,1,500,nullptr); DragAcceptFiles(hwnd,TRUE); return 0;
    case WM_SIZE: { RECT r{}; GetClientRect(hwnd,&r); app->width=MulDiv(r.right,96,app->dpi); app->height=MulDiv(r.bottom,96,app->dpi); app->layout(); app->invalidate(); return 0; }
    case WM_DPICHANGED: { app->dpi=HIWORD(w); auto r=reinterpret_cast<RECT*>(l); SetWindowPos(hwnd,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE); app->fonts(); app->reveal(); return 0; }
    case WM_GETMINMAXINFO: { auto mm=reinterpret_cast<MINMAXINFO*>(l); mm->ptMinTrackSize={MulDiv(440,app->dpi,96),MulDiv(280,app->dpi,96)}; HMONITOR monitor=MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST); MONITORINFO mi{}; mi.cbSize=sizeof(mi); GetMonitorInfoW(monitor,&mi); mm->ptMaxPosition={mi.rcWork.left-mi.rcMonitor.left,mi.rcWork.top-mi.rcMonitor.top}; mm->ptMaxSize={mi.rcWork.right-mi.rcWork.left,mi.rcWork.bottom-mi.rcWork.top}; return 0; }
    case WM_NCCALCSIZE: if(w) return 0; break;
    // The whole window, including its frame, is owned by the custom renderer.
    // DefWindowProc must not repaint native non-client chrome over our canvas.
    case WM_NCPAINT: return 0;
    case WM_NCACTIVATE: return TRUE;
    case WM_NCHITTEST: {
        POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)}; ScreenToClient(hwnd,&p); RECT r{}; GetClientRect(hwnd,&r); int b=MulDiv(4,app->dpi,96);
        if(!IsZoomed(hwnd) && app->dialog==Dialog::None) { bool left=p.x<b,right=p.x>=r.right-b,top=p.y<b,bottom=p.y>=r.bottom-b; if(top&&left) return HTTOPLEFT; if(top&&right) return HTTOPRIGHT; if(bottom&&left) return HTBOTTOMLEFT; if(bottom&&right) return HTBOTTOMRIGHT; if(left) return HTLEFT; if(right) return HTRIGHT; if(top) return HTTOP; if(bottom) return HTBOTTOM; }
        return HTCLIENT;
    }
    case WM_PAINT: app->paint(); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_KEYDOWN: app->key(w); return 0;
    case WM_CHAR: app->character(static_cast<wchar_t>(w)); return 0;
    case WM_SYSKEYDOWN:
        if(w==VK_F4) { if(app->dialog!=Dialog::None) app->cancelDialog(); else app->request(Pending::Exit); return 0; }
        if(app->dialog==Dialog::None) { int index=w=='F'?0:w=='E'?1:w=='S'?2:w=='H'?3:-1; if(index>=0) { app->activeMenu=index; app->hover=-1; app->invalidate(); return 0; } }
        break;
    case WM_SYSCHAR: return 0;
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: { auto p=point(); app->mouseDown(p.x,p.y,msg==WM_LBUTTONDBLCLK); return 0; }
    case WM_MOUSEMOVE: { auto p=point(); app->mouseMove(p.x,p.y); return 0; }
    case WM_LBUTTONUP: { auto p=point(); app->mouseUp(p.x,p.y); return 0; }
    case WM_CAPTURECHANGED: app->selecting=false; app->inputSelecting=false; app->scrollDrag=0; app->titlePress=-1; return 0;
    case WM_MOUSEWHEEL: if(app->dialog==Dialog::None) { int amount=GET_WHEEL_DELTA_WPARAM(w)/WHEEL_DELTA*3; if(GET_KEYSTATE_WPARAM(w)&MK_SHIFT) app->left=std::clamp(app->left-amount,0,app->horizontal.max); else app->top=std::clamp(app->top-amount,0,app->vertical.max); app->invalidate(); } return 0;
    case WM_SETCURSOR: if(LOWORD(l)==HTCLIENT) { POINT p{}; GetCursorPos(&p); ScreenToClient(hwnd,&p); p.x=MulDiv(p.x,96,app->dpi); p.y=MulDiv(p.y,96,app->dpi); bool edit=app->dialog==Dialog::None?app->activeMenu<0 && App::inside(app->editorRect(),p.x,p.y):app->dialogFocus==0 && App::inside(app->inputRect(),p.x,p.y); SetCursor(LoadCursorW(nullptr,edit?IDC_IBEAM:IDC_ARROW)); return TRUE; } break;
    case WM_SETFOCUS: app->focused=true; app->blink=true; app->invalidate(); return 0;
    case WM_KILLFOCUS: app->focused=false; app->activeMenu=-1; app->invalidate(); return 0;
    case WM_ACTIVATE: if(LOWORD(w)==WA_INACTIVE) { app->activeMenu=-1; app->invalidate(); } break;
    case WM_TIMER: if(w==1) app->blinkCaret(); return 0;
    case WM_DROPFILES: { auto drop=reinterpret_cast<HDROP>(w); UINT n=DragQueryFileW(drop,0,nullptr,0); std::wstring p(n+1,L'\0'); DragQueryFileW(drop,0,p.data(),n+1); p.resize(n); DragFinish(drop); if(app->dialog==Dialog::None) { app->droppedPath=p; app->request(Pending::Drop); } return 0; }
    case WM_CLOSE: if(app->dialog!=Dialog::None) app->cancelDialog(); else app->request(Pending::Exit); return 0;
    case WM_DESTROY: KillTimer(hwnd,1); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd,msg,w,l);
}
}

#ifndef RETRONP_TEST
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HICON appIcon=createNotepadIcon();
    WNDCLASSEXW wc{}; wc.cbSize=sizeof(wc); wc.style=CS_DBLCLKS; wc.lpfnWndProc=windowProc; wc.hInstance=instance; wc.hCursor=LoadCursorW(nullptr,IDC_ARROW); wc.lpszClassName=L"RetroNP.Canvas"; wc.hIcon=appIcon; wc.hIconSm=appIcon;
    if(!RegisterClassExW(&wc)) return 1;
    App app; int scale=static_cast<int>(GetDpiForSystem());
    HWND hwnd=CreateWindowExW(WS_EX_ACCEPTFILES,wc.lpszClassName,L"Untitled - RetroNP",WS_POPUP|WS_THICKFRAME|WS_MINIMIZEBOX|WS_MAXIMIZEBOX|WS_SYSMENU,CW_USEDEFAULT,CW_USEDEFAULT,MulDiv(960,scale,96),MulDiv(660,scale,96),nullptr,nullptr,instance,&app);
    if(!hwnd) return 1;
    int argc=0; LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(argc>1 && std::wstring(argv[1])==L"--demo") {
        app.editor.reset(L"#include <iostream>\n\nint main()\n{\n    std::cout << \"Hello World!\\n\";\n    return 0;\n}\n"); app.path=L"hello.cpp"; app.updateTitle();
    } else if(argc>1) app.load(argv[1]);
    if(argv) LocalFree(argv);
    ShowWindow(hwnd,show); UpdateWindow(hwnd);
    MSG msg{}; int result; while((result=GetMessageW(&msg,nullptr,0,0))>0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    if(appIcon) DestroyIcon(appIcon);
    return result<0?1:static_cast<int>(msg.wParam);
}
#endif
