#pragma once

// Compare successive frames from the actual presenter, including fractional DPI.
// These checks do not inject input or capture the desktop.
struct TestSurface {
    HDC dc=nullptr;
    HBITMAP bitmap=nullptr;
    HGDIOBJ original=nullptr;
    DWORD* pixels=nullptr;
    int width,height;
    TestSurface(int w,int h):width(w),height(h) {
        BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=w; info.bmiHeader.biHeight=-h;
        info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
        dc=CreateCompatibleDC(nullptr); void* data=nullptr;
        bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&data,nullptr,0);
        check(dc && bitmap && data,"create rendering test surface");
        pixels=static_cast<DWORD*>(data); original=SelectObject(dc,bitmap);
    }
    ~TestSurface() { SelectObject(dc,original); DeleteObject(bitmap); DeleteDC(dc); }
    RECT rect() const { return {0,0,width,height}; }
    std::vector<DWORD> snapshot() const { GdiFlush(); return {pixels,pixels+width*height}; }
};

void checkBlinkFrames(App& app,TestSurface& surface) {
    app.blink=true; app.invalidate(); check(app.present(surface.dc,surface.rect()),"present initial frame");
    HDC canvas=app.canvas; HBITMAP bitmap=app.canvasBitmap;
    auto previous=surface.snapshot();
    for(int frame=0;frame<8;++frame) {
        ValidateRect(app.hwnd,nullptr);
        app.blinkCaret();
        // Windows discards update regions for hidden test windows. Visible-window
        // invalidation is checked separately in the running app.
        RECT update{},client{}; GetClientRect(app.hwnd,&client);
        if(IsWindowVisible(app.hwnd)) {
            check(GetUpdateRect(app.hwnd,&update,FALSE)!=0,"blink invalidates a visible caret");
            RECT expected=app.deviceRect(app.caretBounds(),client);
            check(EqualRect(&update,&expected)!=0,"blink invalidates only the caret rectangle");
        }
        check(!app.canvasDirty,"blink does not rerender the document");
        check(app.present(surface.dc,surface.rect()),"present blink frame");
        auto current=surface.snapshot(); RECT caret=app.deviceRect(app.caretBounds(),surface.rect());
        size_t changed=0;
        for(int y=0;y<surface.height;++y) for(int x=0;x<surface.width;++x) {
            size_t p=static_cast<size_t>(y)*surface.width+x;
            if(previous[p]!=current[p]) { ++changed; check(App::inside(caret,x,y),"pixels outside caret remain unchanged"); }
        }
        check(changed>0,"caret actually blinks");
        check(app.canvas==canvas && app.canvasBitmap==bitmap,"blink reuses the backing image");
        previous=std::move(current);
    }
}

void checkActiveRepaints(App& app,TestSurface& surface) {
    app.dialog=Dialog::None; app.activeMenu=-1; app.hover=-1; app.showOverwrite=false;
    app.eatCharacter=false; app.wrap=false; app.top=app.left=0; app.editor.reset(); app.layout();
    app.invalidate(); app.present(surface.dc,surface.rect());
    auto previous=surface.snapshot();
    // A keydown that will produce WM_CHAR must not invalidate an unchanged frame.
    app.key('T'); check(!app.canvasDirty,"printable keydown does not repaint before WM_CHAR");
    auto editor=app.editorRect(); RECT staticArea=app.deviceRect({0,editor.top+app.lh+3,app.width,app.height},surface.rect());
    for(wchar_t c:std::wstring(L"Typing repaint.")) {
        app.character(c); check(app.canvasDirty,"typed character invalidates content");
        app.present(surface.dc,surface.rect()); auto current=surface.snapshot();
        for(int y=staticArea.top;y<surface.height;++y) for(int x=0;x<surface.width;++x) {
            size_t p=static_cast<size_t>(y)*surface.width+x;
            check(current[p]==previous[p],"typing preserves the rest of the document and frame");
        }
        previous=std::move(current);
    }
    app.activeMenu=0; app.hover=0; app.invalidate(); app.present(surface.dc,surface.rect());
    RECT menu=app.menuRect(); previous=surface.snapshot();
    for(int step=0;step<12;++step) {
        app.mouseMove(menu.left+35+step,menu.top+8);
        check(!app.canvasDirty,"mouse movement within the same menu row does not repaint");
    }
    RECT changedArea=app.deviceRect(menu,surface.rect());
    for(int step=0;step<12;++step) {
        app.mouseMove(menu.left+35,menu.top+3+(step%2?0:19)+8);
        check(app.canvasDirty,"moving to a different menu row changes the highlight");
        app.present(surface.dc,surface.rect()); auto current=surface.snapshot();
        for(int y=0;y<surface.height;++y) for(int x=0;x<surface.width;++x) {
            if(!App::inside(changedArea,x,y)) {
                size_t p=static_cast<size_t>(y)*surface.width+x;
                check(current[p]==previous[p],"menu hover preserves every pixel outside the popup");
            }
        }
        previous=std::move(current);
    }
    app.activeMenu=-1; app.invalidate();
}

void checkRendering(App& app) {
    KillTimer(app.hwnd,1);
    app.focused=true; app.activeMenu=-1; app.top=app.left=0;
    app.editor.reset(L"A stationary document\nOnly the caret should change.");
    app.editor.move(5,false); app.layout();
    for(int scale:{96,216}) {
        TestSurface surface(MulDiv(app.width,scale,96),MulDiv(app.height,scale,96));
        app.dialog=Dialog::None; app.showOverwrite=false;
        checkBlinkFrames(app,surface);
        app.show(Dialog::Find,L"caret"); checkBlinkFrames(app,surface);
        // Closing a dialog must replace its cached image with the document.
        app.cancelDialog(); check(app.canvasDirty,"closing dialog invalidates cached image");
        app.present(surface.dc,surface.rect()); auto document=surface.snapshot();
        app.editor.replace(L"new text"); app.edited(); app.present(surface.dc,surface.rect());
        check(surface.snapshot()!=document,"text edits refresh the backing image");
        checkActiveRepaints(app,surface);
    }
    TestSurface surface(app.width,app.height);
    app.editor.reset(); app.layout(); app.message=L"Example message"; app.messageTitle=L"RetroNP";
    for(Dialog kind:{Dialog::Open,Dialog::Save,Dialog::Find,Dialog::Confirm,Dialog::Message,Dialog::About}) {
        app.show(kind); app.present(surface.dc,surface.rect()); auto pixels=surface.snapshot(); RECT r=app.dialogRect();
        for(int y=r.top+4;y<r.bottom+4;++y) for(int x=r.left+4;x<r.right+4;++x) {
            if(x>=r.right || y>=r.bottom) check((pixels[static_cast<size_t>(y)*surface.width+x]&0xFFFFFF)==0xFFFFFF,"dialog does not draw a drop shadow");
        }
    }
    app.showOverwrite=true; app.invalidate(); app.present(surface.dc,surface.rect());
    auto pixels=surface.snapshot(); RECT r=app.dialogRect();
    check((pixels[static_cast<size_t>(r.bottom+1)*surface.width+r.right+1]&0xFFFFFF)==0xFFFFFF,"overwrite dialog has no drop shadow");
    app.cancelDialog();
    for(int state=0;state<3;++state) {
        app.focused=state!=0; app.activeMenu=state==1?0:-1; app.dialog=state==2?Dialog::About:Dialog::None;
        app.invalidate(); app.present(surface.dc,surface.rect()); ValidateRect(app.hwnd,nullptr);
        app.blinkCaret(); RECT update{};
        check(!GetUpdateRect(app.hwnd,&update,FALSE),"idle focus/menu/non-editing dialog does not repaint");
    }
    app.focused=true; app.activeMenu=-1; app.cancelDialog();
    std::cout << "Rendering checks passed at 100%/225%: blinking, typing, menu hover, and unchanged menu movement.\n";
}
