/* HikariRO Bestiary visual panel. Included by legacy ddraw.c.
 * Wire protocol: HROBEST|B|page|total; HROBEST|M|id|state|kills|lv|hp|race|element|name; HROBEST|E;
 * State 0 never transmits a monster name or stats.
 */
#define HROBEST_WM_RECORD (WM_APP + 173)
typedef struct { int id,state,kills,lv,hp,race,element; char name[56]; } HroBestRow;
static HWND hrobest_window_;
static HroBestRow hrobest_rows_[24];
static int hrobest_count_,hrobest_page_,hrobest_total_,hrobest_selected_=-1;
static char hrobest_tail_[256];
static int hrobest_tail_length_;
static void hrobest_draw(HDC dc) {
    RECT bounds={0,0,860,570};
    HBRUSH bg=CreateSolidBrush(RGB(232,242,247));
    FillRect(dc,&bounds,bg); DeleteObject(bg);
    SetBkMode(dc,TRANSPARENT);
    SetTextColor(dc,RGB(36,75,101));
    HFONT title=CreateFontA(27,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,0,0,"Segoe UI");
    HFONT regular=CreateFontA(17,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,0,0,"Segoe UI");
    SelectObject(dc,title); TextOutA(dc,25,16,"HIKARIRO  /  BESTIARY",21);
    SelectObject(dc,regular);
    char label[128];
    wsprintfA(label,"Monsters: %d   Page: %d   (Click a tile to inspect)",hrobest_total_,hrobest_page_+1);
    TextOutA(dc,26,55,label,lstrlenA(label));
    HBRUSH card=CreateSolidBrush(RGB(250,250,246));
    RECT left={20,88,514,520},right={526,88,840,520};
    FillRect(dc,&left,card); FillRect(dc,&right,card); DeleteObject(card);
    for(int i=0;i<hrobest_count_;++i) {
        int x=32+(i%4)*119,y=102+(i/4)*69;
        RECT tile={x,y,x+108,y+62};
        HBRUSH brush=CreateSolidBrush(i==hrobest_selected_?RGB(247,209,141):hrobest_rows_[i].state==2?RGB(206,235,219):hrobest_rows_[i].state==1?RGB(215,232,243):RGB(222,224,226));
        FillRect(dc,&tile,brush); DeleteObject(brush);
        Rectangle(dc,tile.left,tile.top,tile.right,tile.bottom);
        const HroBestRow* row=&hrobest_rows_[i];
        char text[80];
        if(row->state==0) lstrcpyA(text,"????");
        else wsprintfA(text,"#%d",row->id);
        TextOutA(dc,x+8,y+7,text,lstrlenA(text));
        lstrcpynA(text,row->state?row->name:"UNKNOWN",sizeof(text));
        TextOutA(dc,x+8,y+29,text,lstrlenA(text)>13?13:lstrlenA(text));
    }
    if(hrobest_selected_>=0 && hrobest_selected_<hrobest_count_) {
        const HroBestRow* row=&hrobest_rows_[hrobest_selected_];
        const char* name=row->state?row->name:"UNKNOWN";
        TextOutA(dc,543,111,name,lstrlenA(name));
        if(row->state==0) lstrcpyA(label,"Not discovered");
        else if(row->state==1) lstrcpyA(label,"Discovered - defeat to unlock");
        else wsprintfA(label,"Defeats: %d",row->kills);
        TextOutA(dc,543,151,label,lstrlenA(label));
        if(row->state==2) {
            wsprintfA(label,"Level: %d   HP: %d",row->lv,row->hp);
            TextOutA(dc,543,187,label,lstrlenA(label));
            wsprintfA(label,"Race: %d   Element: %d",row->race,row->element);
            TextOutA(dc,543,217,label,lstrlenA(label));
        }
    } else TextOutA(dc,543,112,"Select a monster",16);
    TextOutA(dc,30,537,"< PREV",6);
    TextOutA(dc,425,537,"NEXT >",6);
    TextOutA(dc,780,20,"[X]",3);
    DeleteObject(title); DeleteObject(regular);
}
static LRESULT CALLBACK hrobest_proc(HWND hwnd,UINT msg,WPARAM w,LPARAM l) {
    if(msg==HROBEST_WM_RECORD) {
        char* data=(char*)l;
        if(data) {
            if(data[0]=='B') {
                int page=0,total=0;
                if(sscanf(data,"B|%d|%d",&page,&total)==2) {
                    hrobest_page_=page; hrobest_total_=total;
                    hrobest_count_=0; hrobest_selected_=-1;
                    RECT game_rect;
                    if(game_ && GetWindowRect(game_,&game_rect)) {
                        int gx=game_rect.left+(game_rect.right-game_rect.left-860)/2;
                        int gy=game_rect.top+(game_rect.bottom-game_rect.top-570)/2;
                        SetWindowPos(hwnd,HWND_TOP,gx,gy,860,570,SWP_SHOWWINDOW);
                    } else ShowWindow(hwnd,SW_SHOWNORMAL);
                    // Activate and focus this interactive window rather than the RO game window.
                    SetWindowPos(hwnd,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);
                    SetWindowPos(hwnd,HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);
                    SetForegroundWindow(hwnd);
                    SetActiveWindow(hwnd);
                    SetFocus(hwnd);
                }
            } else if(data[0]=='M' && hrobest_count_<24) {
                HroBestRow row={0};
                if(sscanf(data,"M|%d|%d|%d|%d|%d|%d|%d|%55[^;]",&row.id,&row.state,&row.kills,&row.lv,&row.hp,&row.race,&row.element,row.name)==8)
                    hrobest_rows_[hrobest_count_++]=row;
            }
            HeapFree(GetProcessHeap(),0,data);
            InvalidateRect(hwnd,NULL,TRUE);
        }
        return 0;
    }
    if(msg==WM_MOUSEACTIVATE)return MA_ACTIVATE;
    if(msg==WM_SETCURSOR) { SetCursor(LoadCursor(NULL,IDC_ARROW));return TRUE; }
    if(msg==WM_ERASEBKGND)return 1;
    if(msg==WM_PAINT) { PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);hrobest_draw(dc);EndPaint(hwnd,&ps);return 0; }
    if(msg==WM_KEYDOWN && w==VK_ESCAPE) {ShowWindow(hwnd,SW_HIDE);return 0;}
    if(msg==WM_LBUTTONDOWN) {
        int x=LOWORD(l),y=HIWORD(l);
        if(x>765 && y<48) ShowWindow(hwnd,SW_HIDE);
        else if(y>=102 && y<516 && x>=32 && x<508) {
            int col=(x-32)/119,row=(y-102)/69,index=row*4+col;
            if(index>=0 && index<hrobest_count_)hrobest_selected_=index;
        }
        InvalidateRect(hwnd,NULL,TRUE);
        return 0;
    }
    if(msg==WM_CLOSE){ShowWindow(hwnd,SW_HIDE);return 0;}
    return DefWindowProcA(hwnd,msg,w,l);
}
static void hrobest_create(void) {
    WNDCLASSA wc={0};wc.lpfnWndProc=hrobest_proc;
    wc.hCursor=LoadCursor(NULL,IDC_ARROW);
    wc.hInstance=GetModuleHandleA("ddraw.dll");
    wc.lpszClassName="HROBestiaryWindow";
    RegisterClassA(&wc);
    hrobest_window_=CreateWindowExA(WS_EX_TOOLWINDOW,wc.lpszClassName,"HikariRO Bestiary",
        WS_POPUP|WS_CLIPSIBLINGS,0,0,860,570,NULL,NULL,wc.hInstance,NULL);
    if(hrobest_window_) {
        ShowWindow(hrobest_window_,SW_HIDE);
    }
}
/* Reassemble markers across recv boundaries and suppress the complete
 * control message before Ragnarok's normal chat renderer sees it. */
static void hrobest_receive(char* data,int length) {
    static const char marker[]="HROBEST|";
    char merged[8448];
    int copy=length>8192?8192:length;
    if(copy<=0)return;
    memcpy(merged,hrobest_tail_,hrobest_tail_length_);
    memcpy(merged+hrobest_tail_length_,data+length-copy,copy);
    int total=hrobest_tail_length_+copy;
    int incomplete=-1;
    for(int i=0;i+8<=total;i++) {
        if(memcmp(merged+i,marker,8))continue;
        int end=i+8;
        while(end<total && merged[end]!=';' && end-i<240)end++;
        if(end>=total || merged[end]!=';') {incomplete=i;break;}
        int n=end-i-8;
        char* record=(char*)HeapAlloc(GetProcessHeap(),0,n+1);
        if(record && hrobest_window_) {
            memcpy(record,merged+i+8,n);record[n]=0;
            if(!PostMessageA(hrobest_window_,HROBEST_WM_RECORD,0,(LPARAM)record))
                HeapFree(GetProcessHeap(),0,record);
        } else if(record)HeapFree(GetProcessHeap(),0,record);
        i=end;
    }
    if(incomplete>=0) {
        hrobest_tail_length_=total-incomplete;
        if(hrobest_tail_length_>255)hrobest_tail_length_=255;
        memcpy(hrobest_tail_,merged+total-hrobest_tail_length_,hrobest_tail_length_);
    } else {
        hrobest_tail_length_=total>7?7:total;
        memcpy(hrobest_tail_,merged+total-hrobest_tail_length_,hrobest_tail_length_);
    }
    for(int i=0;i+8<=length;i++) {
        if(memcmp(data+i,marker,8))continue;
        data[i]=0;
    }
    for(int n=7;n>=4;n--) {
        if(length>=n && !memcmp(data+length-n,marker,n)) {
            data[length-n]=0;break;
        }
    }
    /* If the previous receive ended with a partial prefix, prevent the
       continuation from reaching the normal chat renderer. */
    if(total>copy && length>0) {
        for(int n=1;n<8;n++) {
            if(hrobest_tail_length_>=n && length>=8-n &&
               !memcmp(merged+total-copy-n,marker,n) &&
               !memcmp(data,marker+n,8-n)) {
                data[0]=0;break;
            }
        }
    }
}
