/* HikariRO Bestiary visual panel. Included by legacy ddraw.c.
 * Wire protocol: HROBEST|B|page|total; HROBEST|M|id|state|kills|lv|hp|race|element|name; HROBEST|E;
 * State 0 never transmits a monster name or stats.
 */
/* Item image loader is implemented later in ddraw.c. */
static HBITMAP load_item_image(int item_id, const char* resource_name);
static BOOL find_item_description(int id, char* output, int output_length);
#define HROBEST_WM_RECORD (WM_APP + 173)
typedef struct { int id,state,kills,lv,hp,race,element,size; char name[56]; int stats[12]; int drop_count; struct { int id,rate; char name[48]; } drops[12]; } HroBestRow;
static HWND hrobest_window_;
static HroBestRow hrobest_rows_[12];
static int hrobest_count_,hrobest_page_,hrobest_total_,hrobest_selected_=-1;
static char hrobest_tail_[256];
static int hrobest_tail_length_;
#define HROBEST_MAX_SPECIES 12000
static HroBestRow hrobest_catalog_[HROBEST_MAX_SPECIES];
static int hrobest_catalog_count_;
static int hrobest_filtered_[HROBEST_MAX_SPECIES],hrobest_filtered_count_;
static char hrobest_search_[48];
static int hrobest_search_active_;
static int hrobest_drop_offset_;
static int hrobest_thumb_budget_;
/* Catalog IDs arrive sorted. Avoid O(species^2) linear searches for
 * thousands of name, status and drop records during window opening. */
static HroBestRow* hrobest_find_catalog(int id) {
    int lo=0,hi=hrobest_catalog_count_-1;
    while(lo<=hi) {
        int mid=lo+(hi-lo)/2;
        if(hrobest_catalog_[mid].id==id)return &hrobest_catalog_[mid];
        if(hrobest_catalog_[mid].id<id)lo=mid+1;
        else hi=mid-1;
    }
    return NULL;
}

#define HROBEST_ICON_CACHE 128
static struct {int id;HBITMAP bitmap;int attempted;} hrobest_icons_[HROBEST_ICON_CACHE];
static int hrobest_icon_cursor_;
static HBITMAP hrobest_icon_for(int id) {
    int i,slot=-1;
    for(i=0;i<HROBEST_ICON_CACHE;i++) {
        if(hrobest_icons_[i].attempted && hrobest_icons_[i].id==id)
            return hrobest_icons_[i].bitmap;
        if(!hrobest_icons_[i].attempted && slot<0)slot=i;
    }
    if(slot<0) {
        slot=hrobest_icon_cursor_++%HROBEST_ICON_CACHE;
        if(hrobest_icons_[slot].bitmap)DeleteObject(hrobest_icons_[slot].bitmap);
    }
    hrobest_icons_[slot].id=id;
    hrobest_icons_[slot].attempted=1;
    hrobest_icons_[slot].bitmap=load_item_image(id,"");
    return hrobest_icons_[slot].bitmap;
}

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
    if(page<0 || page*12>=hrobest_filtered_count_)return;
    hrobest_page_=page;
    hrobest_count_=hrobest_filtered_count_-page*12;
    if(hrobest_count_>12)hrobest_count_=12;
    for(int i=0;i<hrobest_count_;i++)hrobest_rows_[i]=hrobest_catalog_[hrobest_filtered_[page*12+i]];
    hrobest_selected_=-1;
    hrobest_drop_offset_=0;
    hrobest_reset_sprite();
    hrobest_clear_thumbs();
    hrobest_thumb_budget_=0;
    if(hrobest_window_)InvalidateRect(hrobest_window_,NULL,FALSE);
}

/* LRU cache survives page changes; never decode the same monster twice
 * during one client session. Missing sprites are cached too. */
#define HROBEST_SPRITE_CACHE 192
static struct {int id;int state;HBITMAP bmp;unsigned int age;} hrobest_sprite_cache_[HROBEST_SPRITE_CACHE];
static unsigned int hrobest_sprite_clock_;
static HBITMAP hrobest_thumbs_[12];
static int hrobest_thumb_ids_[12];
static void hrobest_clear_thumbs(void) {
    for(int i=0;i<12;i++) {hrobest_thumbs_[i]=NULL;hrobest_thumb_ids_[i]=0;}
}
static int hrobest_cached_sprite(int id,int state,HBITMAP* bitmap) {
    for(int i=0;i<HROBEST_SPRITE_CACHE;i++)if(hrobest_sprite_cache_[i].id==id && hrobest_sprite_cache_[i].state==state) {
        hrobest_sprite_cache_[i].age=++hrobest_sprite_clock_;
        *bitmap=hrobest_sprite_cache_[i].bmp;return 1;
    }
    return 0;
}
static void hrobest_store_sprite(int id,int state,HBITMAP bitmap) {
    int slot=-1;
    for(int i=0;i<HROBEST_SPRITE_CACHE;i++)
        if(hrobest_sprite_cache_[i].id==0) {slot=i;break;}
    if(slot<0) {
        slot=0;
        for(int i=1;i<HROBEST_SPRITE_CACHE;i++)
            if(hrobest_sprite_cache_[i].age<hrobest_sprite_cache_[slot].age)slot=i;
        if(hrobest_sprite_cache_[slot].bmp)DeleteObject(hrobest_sprite_cache_[slot].bmp);
    }
    hrobest_sprite_cache_[slot].id=id;
    hrobest_sprite_cache_[slot].state=state;
    hrobest_sprite_cache_[slot].bmp=bitmap;
    hrobest_sprite_cache_[slot].age=++hrobest_sprite_clock_;
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

static void hrobest_make_gray(HBITMAP bitmap) {
    DIBSECTION dib;
    BYTE* pixels;
    int x,y,stride;
    if(!bitmap)return;
    ZeroMemory(&dib,sizeof(dib));
    if(GetObjectA(bitmap,sizeof(dib),&dib)!=sizeof(dib))return;
    if(dib.dsBm.bmBitsPixel!=32 || !dib.dsBm.bmBits)return;
    pixels=(BYTE*)dib.dsBm.bmBits;
    stride=dib.dsBm.bmWidthBytes;
    for(y=0;y<dib.dsBm.bmHeight;y++) {
        for(x=0;x<dib.dsBm.bmWidth;x++) {
            BYTE* pixel=pixels+y*stride+x*4;
            BYTE luminance;
            if(pixel[0]==255 && pixel[1]==0 && pixel[2]==255)continue;
            luminance=(BYTE)((pixel[2]*77+pixel[1]*150+pixel[0]*29)>>8);
            pixel[0]=luminance;
            pixel[1]=luminance;
            pixel[2]=luminance;
        }
    }
}
static void hrobest_draw_thumbnail(HDC dc,int slot,const HroBestRow* row,int x,int y) {
    if(slot<0||slot>=12)return;
    if(hrobest_thumb_ids_[slot]!=row->id) {
        HBITMAP cached=NULL;
        if(!hrobest_cached_sprite(row->id,row->state==0?0:1,&cached)) {
            if(hrobest_thumb_budget_<=0)return;
            --hrobest_thumb_budget_;
            cached=hrobest_load_sprite_bitmap(row->id,row->name);
            if(row->state==0)hrobest_make_gray(cached);
            hrobest_store_sprite(row->id,row->state==0?0:1,cached);
        }
        hrobest_thumbs_[slot]=cached;
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

static int hrobest_hover_badge_; /* 0 none, 1 race, 2 element, 3 size */
static int hrobest_tab_; /* 0 drops, 1 stats, 2 location */
/* Compact graphic emblems, rendered with GDI rather than letter badges.
 * Race: distinct silhouette; element: distinct shape and palette. */
static void hrobest_draw_emblem(HDC dc,int x,int y,int type,int element) {
    static const COLORREF colors[10]={
        RGB(179,179,202),RGB(90,166,227),RGB(180,144,92),
        RGB(244,108,67),RGB(134,203,146),RGB(164,109,194),
        RGB(247,217,119),RGB(108,91,153),RGB(178,215,231),RGB(136,169,128)
    };
    int kind=(type>=0&&type<10)?type:0;
    COLORREF color=element?colors[kind]:RGB(226,200,151);
    HBRUSH back=CreateSolidBrush(RGB(67,58,92));
    HBRUSH fill=CreateSolidBrush(color);
    HPEN border=CreatePen(PS_SOLID,1,RGB(178,157,204));
    HGDIOBJ ob=SelectObject(dc,back),op=SelectObject(dc,border);
    Ellipse(dc,x,y,x+28,y+28);
    SelectObject(dc,fill);
    if(element) {
        POINT diamond[4]={{x+14,y+4},{x+23,y+14},{x+14,y+24},{x+5,y+14}};
        if(kind==1||kind==4||kind==8)Ellipse(dc,x+7,y+5,x+22,y+23);
        else if(kind==3||kind==5||kind==9)Polygon(dc,diamond,4);
        else {
            POINT tri[3]={{x+14,y+4},{x+23,y+22},{x+5,y+22}};
            Polygon(dc,tri,3);
        }
    } else {
        switch(kind) {
        case 0: Ellipse(dc,x+8,y+8,x+20,y+20);break;
        case 1: case 6: {
            POINT horn[3]={{x+7,y+22},{x+14,y+4},{x+21,y+22}};
            Polygon(dc,horn,3);break;
        }
        case 2: case 9: {
            POINT beast[5]={{x+4,y+20},{x+8,y+8},{x+14,y+14},{x+21,y+7},{x+24,y+20}};
            Polygon(dc,beast,5);break;
        }
        case 3: case 4: {
            Ellipse(dc,x+10,y+6,x+18,y+14);
            MoveToEx(dc,x+14,y+13,NULL);LineTo(dc,x+14,y+23);
            MoveToEx(dc,x+7,y+18,NULL);LineTo(dc,x+21,y+18);break;
        }
        default: {
            Ellipse(dc,x+9,y+5,x+19,y+15);
            Rectangle(dc,x+7,y+16,x+21,y+23);break;
        }
        }
    }
    SelectObject(dc,ob);SelectObject(dc,op);
    DeleteObject(fill);DeleteObject(back);DeleteObject(border);
}
static void hrobest_draw(HDC dc);
/* Render at a resolution-independent logical size, and map mouse hits back. */
#define HROBEST_DESIGN_W 860
#define HROBEST_DESIGN_H 570
static void hrobest_fit_to_game(HWND hwnd) {
    if(!game_ || !IsWindow(game_))return;
    RECT client={0};
    if(!GetClientRect(game_,&client))return;
    POINT origin={0,0};
    ClientToScreen(game_,&origin);
    int w=client.right-client.left,h=client.bottom-client.top;
    if(w>0 && h>0)SetWindowPos(hwnd,HWND_TOP,origin.x,origin.y,w,h,SWP_NOACTIVATE);
}
static POINT hrobest_to_logical(HWND hwnd,LPARAM l) {
    RECT rc={0};GetClientRect(hwnd,&rc);
    int w=rc.right-rc.left,h=rc.bottom-rc.top;
    POINT pt={(short)LOWORD(l),(short)HIWORD(l)};
    if(w>0)pt.x=(int)((long long)pt.x*HROBEST_DESIGN_W/w);
    if(h>0)pt.y=(int)((long long)pt.y*HROBEST_DESIGN_H/h);
    return pt;
}
static void hrobest_paint_scaled(HWND hwnd,HDC dc) {
    RECT rc={0};
    int saved;
    if(!GetClientRect(hwnd,&rc))return;
    int w=rc.right-rc.left,h=rc.bottom-rc.top;
    if(w<=0||h<=0)return;
    /* Draw directly at the target resolution: scaling a pre-rendered bitmap
     * blurs every glyph and forces a large bitmap allocation per repaint. */
    saved=SaveDC(dc);
    SetMapMode(dc,MM_ANISOTROPIC);
    SetWindowExtEx(dc,HROBEST_DESIGN_W,HROBEST_DESIGN_H,NULL);
    SetViewportExtEx(dc,w,h,NULL);
    hrobest_draw(dc);
    RestoreDC(dc,saved);
}
static void hrobest_draw(HDC dc) {
    int themed=hrobest_draw_background(dc);
    if(!themed) {
        RECT bounds={0,0,860,570};
        HBRUSH bg=CreateSolidBrush(RGB(42,38,65));
        FillRect(dc,&bounds,bg); DeleteObject(bg);
    }
    SetBkMode(dc,TRANSPARENT);
    SetTextColor(dc,RGB(236,230,244));
    HFONT title=CreateFontA(27,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,"Segoe UI");
    HFONT regular=CreateFontA(17,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,"Segoe UI");
    SelectObject(dc,title); TextOutA(dc,25,16,"HIKARIRO  /  BESTIARY",21);
    SelectObject(dc,regular);
    char label[128];
    wsprintfA(label,"MONSTER ARCHIVE   %d / %d species   Page %d / %d",hrobest_filtered_count_,hrobest_total_,hrobest_page_+1,(hrobest_filtered_count_+11)/12);
    SetTextColor(dc,RGB(206,192,230));
    TextOutA(dc,26,55,label,lstrlenA(label));
    SetTextColor(dc,RGB(236,230,244));
    RECT searchbox={535,45,832,79};
    HBRUSH searchbrush=CreateSolidBrush(RGB(237,231,247));
    if(!themed)FillRect(dc,&searchbox,searchbrush);DeleteObject(searchbrush);
    HBRUSH outline=CreateSolidBrush(RGB(151,132,183));if(!themed)FrameRect(dc,&searchbox,outline);DeleteObject(outline);
    SetTextColor(dc,RGB(57,46,77));
    wsprintfA(label,"Search: %s%s",hrobest_search_,hrobest_search_active_?"|":"");
    TextOutA(dc,544,53,label,lstrlenA(label));
    SetTextColor(dc,RGB(236,230,244));
    HBRUSH card=CreateSolidBrush(RGB(64,55,86));
    RECT left={20,88,514,520},right={526,88,840,520};
    if(!themed) {FillRect(dc,&left,card); FillRect(dc,&right,card);} DeleteObject(card);
    for(int i=0;i<hrobest_count_;++i) {
        int x=(themed?28:32)+(i%4)*120,y=(themed?142:102)+(i/4)*(themed?121:137);
        RECT tile={x,y,x+108,y+(themed?112:125)};
        HBRUSH brush=CreateSolidBrush(i==hrobest_selected_?RGB(134,110,180):hrobest_rows_[i].state==2?RGB(76,104,101):hrobest_rows_[i].state==1?RGB(83,79,112):RGB(82,73,107));
        if(!themed)FillRect(dc,&tile,brush); DeleteObject(brush);
        HBRUSH tileedge=CreateSolidBrush(RGB(162,145,191));if(!themed || i==hrobest_selected_)FrameRect(dc,&tile,tileedge);DeleteObject(tileedge);
        const HroBestRow* row=&hrobest_rows_[i];
        char text[80];
        if(row->state==0) lstrcpyA(text,"???");
        else wsprintfA(text,"#%d",row->id);
        SetTextColor(dc,RGB(253,224,172));
        TextOutA(dc,x+5,y+3,text,lstrlenA(text));
        SetTextColor(dc,RGB(244,240,248));
        lstrcpynA(text,row->state?row->name:"UNDISCOVERED",sizeof(text));
        TextOutA(dc,x+5,y+(themed?92:103),text,lstrlenA(text)>10?10:lstrlenA(text));
        SetTextColor(dc,RGB(236,230,244));
        hrobest_draw_thumbnail(dc,i,row,x+33,y+(themed?33:43));
    }
    if(hrobest_selected_>=0 && hrobest_selected_<hrobest_count_) {
        const HroBestRow* row=&hrobest_rows_[hrobest_selected_];
        const char* name=row->state?row->name:"UNDISCOVERED";
        HFONT heading=CreateFontA(21,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,"Segoe UI");
        HFONT oldfont=(HFONT)SelectObject(dc,heading);
        SetTextColor(dc,RGB(255,227,168));
        TextOutA(dc,540,101,name,lstrlenA(name));
        SelectObject(dc,oldfont);DeleteObject(heading);
        SetTextColor(dc,RGB(236,230,244));
        if(row->state==0)lstrcpyA(label,"Not discovered");
        else if(row->state==1)lstrcpyA(label,"Discovered - defeat to unlock");
        else wsprintfA(label,"Lv. %d    Defeated: %d",row->lv,row->kills);
        TextOutA(dc,540,133,label,lstrlenA(label));
        if(row->state>0)hrobest_draw_sprite(dc,row->id,row->name,684,150,146,105);
        if(row->state==2) {
            const char* races[]={"Formless","Undead","Brute","Plant","Insect","Fish","Demon","Demi-Human","Angel","Dragon"};
            const char* elements[]={"Neutral","Water","Earth","Fire","Wind","Poison","Holy","Shadow","Ghost","Undead"};
            const char* race=(row->race>=0&&row->race<10)?races[row->race]:"Other";
            const char* element=(row->element>=0&&row->element<10)?elements[row->element]:"Other";
            hrobest_draw_emblem(dc,540,155,row->race,0);
            hrobest_draw_emblem(dc,581,155,row->element,1);
            { int radius=row->size==0?4:row->size==1?7:10;
              HBRUSH b=CreateSolidBrush(RGB(247,211,152));HGDIOBJ old=SelectObject(dc,b);
              Ellipse(dc,634-radius,169-radius,634+radius,169+radius);
              SelectObject(dc,old);DeleteObject(b); }
            if(hrobest_hover_badge_) {
                const char* sizes[]={"Small","Medium","Large"};
                const char* value=hrobest_hover_badge_==1?race:hrobest_hover_badge_==2?element:(row->size>=0&&row->size<3?sizes[row->size]:"Unknown");
                const char* prefix=hrobest_hover_badge_==1?"Race: ":hrobest_hover_badge_==2?"Element: ":"Size: ";
                char hint[90];wsprintfA(hint,"%s%s",prefix,value);
                RECT hintbg={615,152,825,181};
                HBRUSH hb=CreateSolidBrush(RGB(30,26,49));
                FillRect(dc,&hintbg,hb);DeleteObject(hb);
                SetTextColor(dc,RGB(255,235,194));
                TextOutA(dc,624,158,hint,lstrlenA(hint));
                SetTextColor(dc,RGB(236,230,244));
            }
            wsprintfA(label,"HP  %d",row->hp);
            TextOutA(dc,540,190,label,lstrlenA(label));
            RECT tabrects[3]={{540,365,630,393},{636,365,726,393},{732,365,824,393}};
            const char* tabnames[]={"DROPS","STATS","LOCATION"};
            for(int t=0;t<3;t++) {
                HBRUSH tabbrush=CreateSolidBrush(t==hrobest_tab_?RGB(155,125,193):RGB(81,70,115));
                if(!themed)FillRect(dc,&tabrects[t],tabbrush);DeleteObject(tabbrush);
                SetTextColor(dc,RGB(250,239,220));
                TextOutA(dc,tabrects[t].left+8,370,tabnames[t],lstrlenA(tabnames[t]));
            }
            SetTextColor(dc,RGB(236,230,244));
            if(hrobest_tab_==1) {
                wsprintfA(label,"ATK %d-%d    DEF %d    MDEF %d",row->stats[0],row->stats[1],row->stats[2],row->stats[3]);
                TextOutA(dc,540,406,label,lstrlenA(label));
                wsprintfA(label,"STR %d   AGI %d   VIT %d",row->stats[4],row->stats[5],row->stats[6]);
                TextOutA(dc,540,433,label,lstrlenA(label));
                wsprintfA(label,"INT %d   DEX %d   LUK %d",row->stats[7],row->stats[8],row->stats[9]);
                TextOutA(dc,540,460,label,lstrlenA(label));
                wsprintfA(label,"Base EXP %d   Job EXP %d",row->stats[10],row->stats[11]);
                TextOutA(dc,540,487,label,lstrlenA(label));
            } else if(hrobest_tab_==2) {
                TextOutA(dc,540,410,"Locations not yet available",27);
                TextOutA(dc,540,442,"Server map data pending.",24);
            } else {
            for(int d=hrobest_drop_offset_;d<row->drop_count && d<hrobest_drop_offset_+3;d++) {
                int yy=402+(d-hrobest_drop_offset_)*24;
                RECT icon={540,yy,561,yy+20};
                HBRUSH ib=CreateSolidBrush(RGB(111,95,134));
                FillRect(dc,&icon,ib);DeleteObject(ib);
                Rectangle(dc,icon.left,icon.top,icon.right,icon.bottom);
                HBITMAP itemicon=hrobest_icon_for(row->drops[d].id);
                if(itemicon) {
                    BITMAP ibmp={0};GetObject(itemicon,sizeof(ibmp),&ibmp);
                    if(ibmp.bmWidth>0 && ibmp.bmHeight>0) {
                        HDC src=CreateCompatibleDC(dc);
                        HGDIOBJ prev=SelectObject(src,itemicon);
                        TransparentBlt(dc,541,yy+1,19,19,src,0,0,ibmp.bmWidth,ibmp.bmHeight,RGB(255,0,255));
                        SelectObject(src,prev);DeleteDC(src);
                    }
                } else TextOutA(dc,546,yy+1,"?",1);
                char itemname[48];lstrcpynA(itemname,row->drops[d].name,sizeof(itemname));
                for(char* p=itemname;*p;p++)if(*p=='_')*p=' ';
                if(lstrlenA(itemname)>20)itemname[20]=0;
                TextOutA(dc,567,yy+1,itemname,lstrlenA(itemname));
                wsprintfA(label,"%d.%02d%%",row->drops[d].rate/100,row->drops[d].rate%100);
                TextOutA(dc,770,yy+1,label,lstrlenA(label));
            }
            if(row->drop_count>3) {
                RECT nav={540,490,824,516};
                HBRUSH navbg=CreateSolidBrush(RGB(236,226,204));
                FillRect(dc,&nav,navbg);DeleteObject(navbg);
                SetTextColor(dc,RGB(90,72,47));
                TextOutA(dc,550,495,"< PREV",6);
                wsprintfA(label,"%d-%d / %d",hrobest_drop_offset_+1,
                    hrobest_drop_offset_+3<row->drop_count?hrobest_drop_offset_+3:row->drop_count,row->drop_count);
                TextOutA(dc,658,495,label,lstrlenA(label));
                TextOutA(dc,760,495,"NEXT >",6);
            }
            }
        }
    } else TextOutA(dc,543,112,"Select a monster",16);
    {
        RECT prev={24,530,122,560},next={408,530,510,560},close={770,12,834,43};
        HBRUSH button=CreateSolidBrush(RGB(121,96,171));
        if(!themed){FillRect(dc,&prev,button);FillRect(dc,&next,button);FillRect(dc,&close,button);}
        DeleteObject(button);
        SetTextColor(dc,RGB(255,245,219));
        TextOutA(dc,40,536,"< PREV",6);
        TextOutA(dc,423,536,"NEXT >",6);
        TextOutA(dc,791,19,"[X]",3);
    }
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
                        hrobest_fit_to_game(hwnd);
                        ShowWindow(hwnd,SW_SHOWNORMAL);
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
            } else if(data[0]=='N' && data[1]=='|') {
                const char* cursor=data+2;
                while(*cursor && *cursor!=';') {
                    char* next=NULL;
                    unsigned long id=strtoul(cursor,&next,10);
                    if(next==cursor || *next!=':')break;
                    cursor=next+1;
                    const char* end=strchr(cursor,',');
                    if(!end)break;
                    int len=(int)(end-cursor);
                    if(len>0 && len<56) {
                        HroBestRow* found=hrobest_find_catalog((int)id);
                        if(found && found->state==0) {
                            memcpy(found->name,cursor,len);
                            found->name[len]=0;
                        }
                    }
                    cursor=end+1;
                }
            } else if(data[0]=='M' && data[1]=='|') {
                HroBestRow row={0};
                if(sscanf(data,"M|%d|%d|%d|%d|%d|%d|%d|%55[^;]",
                    &row.id,&row.state,&row.kills,&row.lv,&row.hp,&row.race,&row.element,row.name)==8) {
                    HroBestRow* found=hrobest_find_catalog(row.id);
                    if(found) {
                        int previous_state=found->state;
                        *found=row;
                        if(previous_state!=row.state) {
                            for(int k=0;k<hrobest_count_;k++)if(hrobest_rows_[k].id==row.id) {
                                hrobest_rows_[k]=row;
                                hrobest_thumb_ids_[k]=0;
                                hrobest_thumbs_[k]=NULL;
                            }
                        }
                    }
                }
            } else if(data[0]=='Z' && data[1]=='|') {
                int id=0,size=0;
                if(sscanf(data,"Z|%d|%d",&id,&size)==2) {
                    HroBestRow* found=hrobest_find_catalog(id);
                    if(found && found->state==2)found->size=size;
                }
            } else if(data[0]=='S' && data[1]=='|') {
                int id=0,v[12]={0};
                if(sscanf(data,"S|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d",
                    &id,&v[0],&v[1],&v[2],&v[3],&v[4],&v[5],&v[6],&v[7],&v[8],&v[9],&v[10],&v[11])==13) {
                    HroBestRow* found=hrobest_find_catalog(id);
                    if(found && found->state==2)memcpy(found->stats,v,sizeof(v));
                }
            } else if(data[0]=='D' && data[1]=='|') {
                int id=0,item_id=0,rate=0;char name[48]={0};
                if(sscanf(data,"D|%d|%d|%d|%47[^;]",&id,&item_id,&rate,name)==4) {
                    HroBestRow* row=hrobest_find_catalog(id);
                    if(row && row->state==2) {
                        if(row->drop_count<12) {
                            int d=row->drop_count++;
                            row->drops[d].id=item_id;row->drops[d].rate=rate;
                            lstrcpynA(row->drops[d].name,name,48);
                        }
                    }
                }
            } else if(data[0]=='E') {
                hrobest_filter();hrobest_show_page(0);
            }
            /* Thousands of catalog records arrive together. Redrawing the full
             * scaled window for every record stalls the client and chat. */
            int refresh=(data[0]=='E' || data[0]=='B');
            HeapFree(GetProcessHeap(),0,data);
            if(refresh)InvalidateRect(hwnd,NULL,FALSE);
        }
        return 0;
    }
    if(msg==WM_TIMER && w==173) {
        if(!IsWindowVisible(hwnd))return 0;
        int pending=0;
        for(int i=0;i<hrobest_count_;i++)
            if(hrobest_thumb_ids_[i]!=hrobest_rows_[i].id) {pending=1;break;}
        if(pending) {
            hrobest_thumb_budget_=1;
            InvalidateRect(hwnd,NULL,FALSE);
        }
        return 0;
    }
    if(msg==WM_MOUSEACTIVATE)return MA_ACTIVATE;
    if(msg==WM_SETCURSOR) { SetCursor(LoadCursor(NULL,IDC_ARROW));return TRUE; }
    if(msg==WM_ERASEBKGND)return 1;
    if(msg==WM_PAINT) { PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);hrobest_paint_scaled(hwnd,dc);EndPaint(hwnd,&ps);return 0; }
    if(msg==WM_SIZE || msg==WM_DISPLAYCHANGE) {InvalidateRect(hwnd,NULL,TRUE);return 0;}
    if(msg==WM_ACTIVATE && LOWORD(w)!=WA_INACTIVE)hrobest_fit_to_game(hwnd);
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
        if(w==VK_ESCAPE) {hrobest_search_active_=0;ShowWindow(hwnd,SW_HIDE);}
        else if(w==VK_RIGHT || w==VK_NEXT)hrobest_request_page(hrobest_page_+1);
        else if(w==VK_LEFT || w==VK_PRIOR)hrobest_request_page(hrobest_page_-1);
        return 0;
    }
    if(msg==WM_MOUSEMOVE) {
        POINT pt=hrobest_to_logical(hwnd,l);
        int hovered=0;
        if(hrobest_selected_>=0 && hrobest_selected_<hrobest_count_ &&
           hrobest_rows_[hrobest_selected_].state==2 && pt.y>=155 && pt.y<183) {
            if(pt.x>=540 && pt.x<568)hovered=1;
            else if(pt.x>=581 && pt.x<609)hovered=2;
            else if(pt.x>=619 && pt.x<649)hovered=3;
        }
        if(hovered!=hrobest_hover_badge_) {
            hrobest_hover_badge_=hovered;
            InvalidateRect(hwnd,NULL,FALSE);
        }
        return 0;
    }
    if(msg==WM_MOUSEWHEEL) { hrobest_request_page(hrobest_page_+(GET_WHEEL_DELTA_WPARAM(w)<0?1:-1));return 0; }
    if(msg==WM_LBUTTONDOWN) {
        POINT pt=hrobest_to_logical(hwnd,l);
        int x=pt.x,y=pt.y;
        if(x>=765 && x<=860 && y>=0 && y<=44) {hrobest_search_active_=0;ShowWindow(hwnd,SW_HIDE);}
        else if(x>=535 && x<=832 && y>=45 && y<=79)hrobest_search_active_=1;
        else if(y>=365 && y<394 && x>=540 && x<825)hrobest_tab_=x<634?0:x<730?1:2;
        else if(hrobest_tab_==0 && y>=490 && y<519 && x>=540 && x<825 && hrobest_selected_>=0 && hrobest_selected_<hrobest_count_) {
            const HroBestRow* row=&hrobest_rows_[hrobest_selected_];
            if(x<640) {if(hrobest_drop_offset_>0)hrobest_drop_offset_-=3;}
            else if(x>=750 && hrobest_drop_offset_+3<row->drop_count)hrobest_drop_offset_+=3;
        }
        else if(hrobest_tab_==0 && x>=540 && x<825 && y>=402 && y<486 && hrobest_selected_>=0 && hrobest_selected_<hrobest_count_) {
            const HroBestRow* selected=&hrobest_rows_[hrobest_selected_];
            int d=hrobest_drop_offset_+(y-402)/27;
            if(selected->state==2 && d>=0 && d<selected->drop_count && (y-402)>=0 && (y-402)%27<23) {
                /* Native item information is owned by the RO client.
                 * Do not show a Windows MessageBox as a misleading substitute.
                 * Keep drop selection inert until the client tooltip bridge exists. */
            }
        }
        else if(y>=525 && x<145)hrobest_request_page(hrobest_page_-1);
        else if(y>=525 && x>=380 && x<525)hrobest_request_page(hrobest_page_+1);
        else if(y>=102 && y<516 && x>=32 && x<508) {
            int col=(x-28)/120,row=(y-142)/121,index=row*4+col;
            if(index>=0 && index<hrobest_count_){hrobest_selected_=index;hrobest_drop_offset_=0;hrobest_reset_sprite();}
        }
        if(!(x>=535 && x<=832 && y>=45 && y<=79))hrobest_search_active_=0;
        InvalidateRect(hwnd,NULL,TRUE);
        return 0;
    }
    if(msg==WM_CLOSE){hrobest_search_active_=0;ShowWindow(hwnd,SW_HIDE);return 0;}
    return DefWindowProcA(hwnd,msg,w,l);
}
static void hrobest_create(void) {
    WNDCLASSA wc={0};wc.lpfnWndProc=hrobest_proc;
    wc.hCursor=LoadCursor(NULL,IDC_ARROW);
    wc.hInstance=GetModuleHandleA("ddraw.dll");
    wc.lpszClassName="HROBestiaryWindow";
    RegisterClassA(&wc);
    hrobest_window_=CreateWindowExA(WS_EX_TOOLWINDOW,wc.lpszClassName,"HikariRO Bestiary",
        WS_POPUP|WS_CLIPSIBLINGS,0,0,860,570,game_,NULL,wc.hInstance,NULL);
    if(hrobest_window_) {
        SetTimer(hrobest_window_,173,35,NULL);
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
        while(end<total && merged[end]!=';' && end-i<255)end++;
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
