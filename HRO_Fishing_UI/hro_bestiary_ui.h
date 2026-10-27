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
#define HROBEST_MAX_SPECIES 12000
static HroBestRow hrobest_catalog_[HROBEST_MAX_SPECIES];
static int hrobest_catalog_count_;
static int hrobest_filtered_[HROBEST_MAX_SPECIES],hrobest_filtered_count_;
static char hrobest_search_[48];
static int hrobest_search_active_;
static int hrobest_contains(const char* name,const char* query) {
    if(!*query)return 1;
    for(const char* p=name;*p;p++) {
        const char* a=p,*b=query;
        while(*a&&*b) {
            char x=*a,y=*b;
            if(x>='A'&&x<='Z')x+=32;
            if(y>='A'&&y<='Z')y+=32;
            if(x!=y)break;
            a++;b++;
        }
        if(!*b)return 1;
    }
    return 0;
}
static void hrobest_filter(void) {
    hrobest_filtered_count_=0;
    for(int i=0;i<hrobest_catalog_count_;i++) {
        HroBestRow* row=&hrobest_catalog_[i];
        char id[20];wsprintfA(id,"%d",row->id);
        if(!hrobest_search_[0] ||
            (row->state>0 && hrobest_contains(row->name,hrobest_search_)) ||
            hrobest_contains(id,hrobest_search_))
            hrobest_filtered_[hrobest_filtered_count_++]=i;
    }
}

static void hrobest_reset_sprite(void);
static void hrobest_clear_thumbs(void);
static void hrobest_show_page(int page) {
    if(page<0 || page*24>=hrobest_filtered_count_)return;
    hrobest_page_=page;
    hrobest_count_=hrobest_filtered_count_-page*24;
    if(hrobest_count_>24)hrobest_count_=24;
    for(int i=0;i<hrobest_count_;i++)hrobest_rows_[i]=hrobest_catalog_[hrobest_filtered_[page*24+i]];
    hrobest_selected_=-1;
    hrobest_reset_sprite();
    hrobest_clear_thumbs();
    if(hrobest_window_)InvalidateRect(hrobest_window_,NULL,TRUE);
}

static HBITMAP hrobest_thumbs_[24];
static int hrobest_thumb_ids_[24];
static void hrobest_clear_thumbs(void) {
    for(int i=0;i<24;i++) {
        if(hrobest_thumbs_[i])DeleteObject(hrobest_thumbs_[i]);
        hrobest_thumbs_[i]=NULL;hrobest_thumb_ids_[i]=0;
    }
}
static HBITMAP hrobest_sprite_;
static int hrobest_sprite_id_=-1;
static void hrobest_reset_sprite(void) {
    if(hrobest_sprite_)DeleteObject(hrobest_sprite_);
    hrobest_sprite_=NULL;hrobest_sprite_id_=-1;
}
static void hrobest_draw_sprite(HDC dc,int mob_id,const char* mob_name,int x,int y,int w,int h) {
    if(hrobest_sprite_id_!=mob_id) {
        hrobest_reset_sprite();
        hrobest_sprite_id_=mob_id;
        hrobest_sprite_=hrobest_load_sprite_bitmap(mob_id,mob_name);
    }
    if(!hrobest_sprite_)return;
    BITMAP bmp={0};GetObject(hrobest_sprite_,sizeof(bmp),&bmp);
    if(!bmp.bmWidth || !bmp.bmHeight)return;
    HDC src=CreateCompatibleDC(dc);
    HGDIOBJ old=SelectObject(src,hrobest_sprite_);
    int width=bmp.bmWidth,height=bmp.bmHeight;
    if(width>w || height>h) {
        double scale=(double)w/width;
        if((double)h/height<scale)scale=(double)h/height;
        width=(int)(width*scale);height=(int)(height*scale);
    }
    SetStretchBltMode(dc,COLORONCOLOR);
    TransparentBlt(dc,x+(w-width)/2,y+(h-height)/2,width,height,
        src,0,0,bmp.bmWidth,bmp.bmHeight,RGB(255,0,255));
    SelectObject(src,old);DeleteDC(src);
}

static void hrobest_draw_thumbnail(HDC dc,int slot,const HroBestRow* row,int x,int y) {
    if(slot<0||slot>=24||row->state==0)return;
    if(hrobest_thumb_ids_[slot]!=row->id) {
        if(hrobest_thumbs_[slot])DeleteObject(hrobest_thumbs_[slot]);
        hrobest_thumbs_[slot]=hrobest_load_sprite_bitmap(row->id,row->name);
        hrobest_thumb_ids_[slot]=row->id;
    }
    HBITMAP bitmap=hrobest_thumbs_[slot];
    if(!bitmap)return;
    BITMAP bmp={0};GetObject(bitmap,sizeof(bmp),&bmp);
    if(bmp.bmWidth<=0||bmp.bmHeight<=0)return;
    int w=bmp.bmWidth,h=bmp.bmHeight;
    if(w>45||h>38) {
        double scale=45.0/w;
        if(38.0/h<scale)scale=38.0/h;
        w=(int)(w*scale);h=(int)(h*scale);
    }
    HDC src=CreateCompatibleDC(dc);
    HGDIOBJ old=SelectObject(src,bitmap);
    TransparentBlt(dc,x+(45-w)/2,y+(38-h)/2,w,h,
        src,0,0,bmp.bmWidth,bmp.bmHeight,RGB(255,0,255));
    SelectObject(src,old);DeleteDC(src);
}
static void hrobest_request_page(int page) {
    hrobest_show_page(page);
}

static void hrobest_draw(HDC dc) {
    RECT bounds={0,0,860,570};
    HBRUSH bg=CreateSolidBrush(RGB(220,231,232));
    FillRect(dc,&bounds,bg); DeleteObject(bg);
    SetBkMode(dc,TRANSPARENT);
    SetTextColor(dc,RGB(63,70,65));
    HFONT title=CreateFontA(27,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,0,0,"Segoe UI");
    HFONT regular=CreateFontA(17,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,0,0,"Segoe UI");
    SelectObject(dc,title); TextOutA(dc,25,16,"HIKARIRO  /  BESTIARY",21);
    SelectObject(dc,regular);
    char label[128];
    wsprintfA(label,"MONSTER ARCHIVE   %d / %d species   Page %d / %d",hrobest_filtered_count_,hrobest_total_,hrobest_page_+1,(hrobest_filtered_count_+23)/24);
    TextOutA(dc,26,55,label,lstrlenA(label));
    RECT searchbox={535,45,832,79};
    HBRUSH searchbrush=CreateSolidBrush(RGB(255,255,255));
    FillRect(dc,&searchbox,searchbrush);DeleteObject(searchbrush);
    Rectangle(dc,searchbox.left,searchbox.top,searchbox.right,searchbox.bottom);
    wsprintfA(label,"Search: %s%s",hrobest_search_,hrobest_search_active_?"|":"");
    TextOutA(dc,544,53,label,lstrlenA(label));
    HBRUSH card=CreateSolidBrush(RGB(251,247,235));
    RECT left={20,88,514,520},right={526,88,840,520};
    FillRect(dc,&left,card); FillRect(dc,&right,card); DeleteObject(card);
    for(int i=0;i<hrobest_count_;++i) {
        int x=32+(i%4)*119,y=102+(i/4)*69;
        RECT tile={x,y,x+108,y+62};
        HBRUSH brush=CreateSolidBrush(i==hrobest_selected_?RGB(240,198,119):hrobest_rows_[i].state==2?RGB(218,229,201):hrobest_rows_[i].state==1?RGB(224,230,222):RGB(231,227,215));
        FillRect(dc,&tile,brush); DeleteObject(brush);
        Rectangle(dc,tile.left,tile.top,tile.right,tile.bottom);
        const HroBestRow* row=&hrobest_rows_[i];
        char text[80];
        if(row->state==0) lstrcpyA(text,"???");
        else wsprintfA(text,"#%d",row->id);
        TextOutA(dc,x+8,y+7,text,lstrlenA(text));
        lstrcpynA(text,row->state?row->name:"UNDISCOVERED",sizeof(text));
        TextOutA(dc,x+8,y+29,text,lstrlenA(text)>13?13:lstrlenA(text));
        if(row->state>0)hrobest_draw_thumbnail(dc,i,row,x+57,y+5);
    }
    if(hrobest_selected_>=0 && hrobest_selected_<hrobest_count_) {
        const HroBestRow* row=&hrobest_rows_[hrobest_selected_];
        const char* name=row->state?row->name:"UNDISCOVERED";
        TextOutA(dc,543,111,"MONSTER DETAILS",15);
        TextOutA(dc,543,149,name,lstrlenA(name));
        if(row->state==0) lstrcpyA(label,"Not discovered");
        else if(row->state==1) lstrcpyA(label,"Discovered - defeat to unlock");
        else wsprintfA(label,"Defeats: %d",row->kills);
        TextOutA(dc,543,190,label,lstrlenA(label));
        if(row->state>0)hrobest_draw_sprite(dc,row->id,row->name,560,310,240,180);
        if(row->state==2) {
            wsprintfA(label,"Level: %d   HP: %d",row->lv,row->hp);
            TextOutA(dc,543,231,label,lstrlenA(label));
            wsprintfA(label,"Race: %d   Element: %d",row->race,row->element);
            TextOutA(dc,543,261,label,lstrlenA(label));
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
            if(data[0]=='C') {
                int total=0;
                if(sscanf(data,"C|%d",&total)==1) {
                    hrobest_total_=total;
                    hrobest_catalog_count_=0;
                    hrobest_filtered_count_=0;
                    hrobest_search_[0]=0;
                    hrobest_page_=0;
                    hrobest_count_=0;
                    hrobest_selected_=-1;
                    hrobest_reset_sprite();
                    if(!IsWindowVisible(hwnd)) {
                        RECT game_rect;
                        if(game_ && GetWindowRect(game_,&game_rect)) {
                            int gx=game_rect.left+(game_rect.right-game_rect.left-860)/2;
                            int gy=game_rect.top+(game_rect.bottom-game_rect.top-570)/2;
                            SetWindowPos(hwnd,HWND_TOP,gx,gy,860,570,SWP_SHOWWINDOW);
                        } else ShowWindow(hwnd,SW_SHOWNORMAL);
                        SetForegroundWindow(hwnd);SetFocus(hwnd);
                    }
                }
            } else if(data[0]=='I' && data[1]=='|') {
                const char* cursor=data+2;
                while(*cursor && hrobest_catalog_count_<HROBEST_MAX_SPECIES) {
                    char* next=NULL;
                    unsigned long id=strtoul(cursor,&next,10);
                    if(next==cursor)break;
                    HroBestRow* row=&hrobest_catalog_[hrobest_catalog_count_++];
                    memset(row,0,sizeof(*row));row->id=(int)id;
                    lstrcpyA(row->name,"UNKNOWN");
                    cursor=(*next==',')?next+1:next;
                }
            } else if(data[0]=='M' && data[1]=='|') {
                HroBestRow row={0};
                if(sscanf(data,"M|%d|%d|%d|%d|%d|%d|%d|%55[^;]",
                    &row.id,&row.state,&row.kills,&row.lv,&row.hp,&row.race,&row.element,row.name)==8) {
                    for(int i=0;i<hrobest_catalog_count_;i++) {
                        if(hrobest_catalog_[i].id==row.id) {
                            hrobest_catalog_[i]=row;break;
                        }
                    }
                }
            } else if(data[0]=='E') {
                hrobest_filter();hrobest_show_page(0);
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
    if(msg==WM_CHAR && hrobest_search_active_) {
        size_t n=strlen(hrobest_search_);
        if(w==8 && n)hrobest_search_[n-1]=0;
        else if(w>=32 && w<127 && n<sizeof(hrobest_search_)-1) {
            hrobest_search_[n]=(char)w;hrobest_search_[n+1]=0;
        } else return 0;
        hrobest_filter();
        if(hrobest_filtered_count_)hrobest_show_page(0);
        else {hrobest_page_=0;hrobest_count_=0;hrobest_selected_=-1;}
        InvalidateRect(hwnd,NULL,TRUE);
        return 0;
    }
    if(msg==WM_KEYDOWN) {
        if(w==VK_ESCAPE) {if(hrobest_search_active_)hrobest_search_active_=0;else ShowWindow(hwnd,SW_HIDE);}
        else if(w==VK_RIGHT || w==VK_NEXT)hrobest_request_page(hrobest_page_+1);
        else if(w==VK_LEFT || w==VK_PRIOR)hrobest_request_page(hrobest_page_-1);
        return 0;
    }
    if(msg==WM_MOUSEWHEEL) { hrobest_request_page(hrobest_page_+(GET_WHEEL_DELTA_WPARAM(w)<0?1:-1));return 0; }
    if(msg==WM_LBUTTONDOWN) {
        int x=LOWORD(l),y=HIWORD(l);
        if(x>765 && y<40) ShowWindow(hwnd,SW_HIDE);
        else if(x>=535 && x<=832 && y>=45 && y<=79)hrobest_search_active_=1;
        else if(y>=525 && x<145)hrobest_request_page(hrobest_page_-1);
        else if(y>=525 && x>=380 && x<525)hrobest_request_page(hrobest_page_+1);
        else if(y>=102 && y<516 && x>=32 && x<508) {
            int col=(x-32)/119,row=(y-102)/69,index=row*4+col;
            if(index>=0 && index<hrobest_count_){hrobest_selected_=index;hrobest_reset_sprite();}
        }
        if(!(x>=535 && x<=832 && y>=45 && y<=79))hrobest_search_active_=0;
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
