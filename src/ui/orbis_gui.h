/* ORBIS OS native GOP framebuffer graphical preview.
 * EXPERIMENTAL: volatile RAM-only demo account, NOT secure authentication.
 * No browser/HTML, untrusted GPU code, persistent secrets or real file GUI.
 */
#ifndef WINUX_ORBIS_GUI_H
#define WINUX_ORBIS_GUI_H
#include "../../build/orbis-gui-preview/orbis_font.h"
extern const unsigned char orbis_wall_rgb[1280U*800U*3U];
extern const unsigned char orbis_icon_bgra[512U*256U*4U];
enum {
    OR_ICON_FILES,OR_ICON_TERMINAL,OR_ICON_APPS,OR_ICON_SETTINGS,
    OR_ICON_INFO_APP,OR_ICON_ACCOUNT,OR_ICON_BROWSER,OR_ICON_PHOTOS,
    OR_ICON_DESKTOP,OR_ICON_DOCUMENTS,OR_ICON_PICTURES,OR_ICON_MUSIC,
    OR_ICON_VIDEOS,OR_ICON_DOWNLOADS,OR_ICON_FOLDER,OR_ICON_BRAND,
    OR_ICON_MONITOR,OR_ICON_GLOBE,OR_ICON_KEYBOARD,OR_ICON_CLOCK,
    OR_ICON_USER,OR_ICON_LOCK,OR_ICON_INFO,OR_ICON_ARROW,
    OR_ICON_HOME,OR_ICON_SEARCH,OR_ICON_POWER,OR_ICON_WIFI,
    OR_ICON_VOLUME,OR_ICON_BATTERY,OR_ICON_NETWORK,OR_ICON_CLOSE
};
enum { OR_SPLASH,OR_SETUP_NAME,OR_SETUP_PIN,OR_LOGIN,OR_DESKTOP,OR_FILES,OR_SETTINGS };
static u32 or_state,or_name_len,or_pin_len,or_attempts,or_guest;
static char or_name[24],or_pin[20];
static u64 or_hash,or_salt,or_lock_until,or_splash_tick;
static int or_dirty,or_entry_dirty;
#define OR_WHITE 0xf7f6f3U
#define OR_GRAY 0xd3d8dfU
#define OR_BLUE 0x267debU
#define OR_PANEL 0x26292fU
#define OR_DARK 0x1d242eU
#define OR_EDGE 0x667483U
static u32 or_x(u32 n){return (u32)((u64)n*boot->width/1280ULL);}
static u32 or_y(u32 n){return (u32)((u64)n*boot->height/720ULL);}
static void or_rect(u32 x,u32 y,u32 w,u32 h,u32 c){
    rectangle(or_x(x),or_y(y),or_x(w)+1,or_y(h)+1,c);
}
static u32 or_font_mix(u32 dst,u32 fg,u32 a){
    u32 inv=255-a;
    return (((((dst>>16)&255)*inv+((fg>>16)&255)*a)/255)<<16)|
           (((((dst>>8)&255)*inv+((fg>>8)&255)*a)/255)<<8)|
           (((dst&255)*inv+(fg&255)*a)/255);
}
static void or_font_glyph(u32 x,u32 y,u32 ch,u32 fg){
    if(ch<32||ch>126)ch='?';
    if(!graphics_ready)return;
    volatile u32 *fb=(volatile u32*)boot->framebuffer;
    const u8 *coverage=orbis_font_8bpp[ch-32];
    for(u32 yy=0;yy<ORBIS_BODY_H;yy++)for(u32 xx=0;xx<ORBIS_BODY_W;xx++){
        u32 u=x+xx,v=y+yy;
        if(u>=boot->width||v>=boot->height)continue;
        u32 offset=yy*ORBIS_BODY_W+xx;
        u32 alpha=coverage[offset];
        if(!alpha)continue;
        u64 index=(u64)v*boot->stride+u;
        fb[index]=rgb(or_font_mix(rgb(fb[index]),fg,alpha));
    }
}
static void or_label(u32 x,u32 y,const char *s,u32 c){
    u32 px=or_x(x),py=or_y(y);
    while(*s && px+ORBIS_BODY_W<boot->width){
        u8 ch=(u8)*s++;or_font_glyph(px,py,ch,c);
        px+=orbis_font_advance[ch>=32&&ch<=126?ch-32:31];
    }
}
static void or_center(u32 y,const char *s,u32 color){
    u32 n=0,w=0;
    while(s[n] && n<100){u8 ch=(u8)s[n++];w+=orbis_font_advance[ch>=32&&ch<=126?ch-32:31];}
    u32 x=boot->width>w?(boot->width-w)/2:0;
    while(*s&&x+ORBIS_BODY_W<boot->width){
        u8 ch=(u8)*s++;or_font_glyph(x,or_y(y),ch,color);
        x+=orbis_font_advance[ch>=32&&ch<=126?ch-32:31];
    }
}
static u32 or_sqrt(u32 n){
    u32 a=0;while((a+1)*(a+1)<=n)a++;return a;
}
static int or_round_sample(int px,int py,int w,int h,int r){
    if(px<0||py<0||px>=w||py>=h)return 0;
    if(px>=r&&px<w-r)return 1;
    if(py>=r&&py<h-r)return 1;
    int cx=px<r?r:w-r-1,cy=py<r?r:h-r-1;
    int dx=px-cx,dy=py-cy;
    return dx*dx+dy*dy<=r*r;
}
static void or_frame(u32 x,u32 y,u32 w,u32 h,u32 c){
    /* One-pixel antialiased rounded perimeter. The same radius used by the
     * glass fill prevents the small square hook previously visible at corners. */
    u32 x0=or_x(x),y0=or_y(y),x1=or_x(x+w),y1=or_y(y+h);
    if(x1>boot->width)x1=boot->width;if(y1>boot->height)y1=boot->height;
    if(x1<=x0+5||y1<=y0+5)return;
    u32 radius=or_x(17);
    if(radius<2)radius=2;
    if(radius>(x1-x0)/2)radius=(x1-x0)/2;
    if(radius>(y1-y0)/2)radius=(y1-y0)/2;
    int pw=(int)(x1-x0),ph=(int)(y1-y0),r4=(int)radius*4;
    volatile u32 *fb=(volatile u32*)boot->framebuffer;
    for(int yy=0;yy<ph;yy++)for(int xx=0;xx<pw;xx++){
        if(yy>(int)radius+1&&yy<ph-(int)radius-2&&xx>1&&xx<pw-2)continue;
        if(xx>(int)radius+1&&xx<pw-(int)radius-2&&yy>1&&yy<ph-2)continue;
        u32 coverage=0;
        for(int sy=1;sy<=3;sy+=2)for(int sx=1;sx<=3;sx+=2){
            int qx=xx*4+sx,qy=yy*4+sy;
            int outer=or_round_sample(qx,qy,pw*4,ph*4,r4);
            int inner=or_round_sample(qx-4,qy-4,(pw-2)*4,(ph-2)*4,r4-4);
            coverage+=(u32)(outer&&!inner);
        }
        if(!coverage)continue;
        u64 at=(u64)(y0+(u32)yy)*boot->stride+x0+(u32)xx;
        fb[at]=rgb(or_font_mix(rgb(fb[at]),c,coverage*255U/4U));
    }
}
static void or_disc(u32 x,u32 y,u32 r,u32 c){
    u32 px=or_x(x),py=or_y(y),rad=or_x(r);
    if(rad<1)rad=1;
    for(int dy=-(int)rad;dy<=(int)rad;dy++){
        int yy=(int)py+dy;if(yy<0||(u32)yy>=boot->height)continue;
        u32 square=rad*rad-(u32)(dy*dy),half=0;
        while((half+1)*(half+1)<=square)half++;
        u32 xx=px>half?px-half:0,ex=px+half;
        if(ex>boot->width)ex=boot->width;
        if(xx<ex)rectangle(xx,(u32)yy,ex-xx,1,c);
    }
}
static u32 or_noise(u32 x,u32 y){
    u32 n=x*374761393U+y*668265263U+0x9e3779b9U;
    n=(n^(n>>13))*1274126177U;return n^(n>>16);
}
static u32 or_blend(u32 a,u32 b,u32 alpha){
    u32 inv=256-alpha;
    return (((((a>>16)&255)*inv+((b>>16)&255)*alpha)>>8)<<16)|
           (((((a>>8)&255)*inv+((b>>8)&255)*alpha)>>8)<<8)|
           (((a&255)*inv+(b&255)*alpha)>>8);
}

static void or_wallpaper(void){
    /* True RGB888 bitmap, 1280x800, no RGB565 banding and no upscale on
     * the QEMU GOP default mode. Image bytes come from a COFF .incbin
     * resource, not a giant C initializer. */
    volatile u32 *fb=(volatile u32*)boot->framebuffer;
    for(u32 y=0;y<boot->height;y++){
        u32 sy=(u32)((u64)y*800U/boot->height);
        for(u32 x=0;x<boot->width;x++){
            u32 sx=(u32)((u64)x*1280U/boot->width);
            u64 at=((u64)sy*1280U+sx)*3U;
            u32 color=((u32)orbis_wall_rgb[at]<<16)|
                      ((u32)orbis_wall_rgb[at+1]<<8)|orbis_wall_rgb[at+2];
            /* Art-directed backdrop treatment: a calm dark falloff behind
             * the brand plus a subtle lower vignette. This replaces the
             * untouched stock-photo appearance without baking UI into it. */
            u32 shade=0;
            if(x*1280U<boot->width*500U)
                shade=(500U-(u32)((u64)x*1280U/boot->width))*76U/500U;
            shade+=(u32)((u64)y*34U/boot->height);
            if(shade>104)shade=104;
            color=or_blend(color,0x09131f,shade);
            fb[(u64)y*boot->stride+x]=rgb(color);
        }
    }
}
/* GPU-independent dark glass simulation: alpha-composite over GOP wallpaper.
 * Pixel alpha is computed in RAM, rather than browser CSS or compositing GPU.
 */
static void or_glass(u32 x,u32 y,u32 w,u32 h,u32 tint,u32 alpha){
    if(!graphics_ready)return;
    u32 x0=or_x(x),y0=or_y(y),x1=or_x(x+w),y1=or_y(y+h);
    if(x1>boot->width)x1=boot->width;
    if(y1>boot->height)y1=boot->height;
    if(alpha>255)alpha=255;
    volatile u32 *fb=(volatile u32*)boot->framebuffer;
    u32 radius=or_x(17);
    if(radius<2)radius=2;
    if(radius>(x1-x0)/2)radius=(x1-x0)/2;
    if(radius>(y1-y0)/2)radius=(y1-y0)/2;
    for(u32 yy=y0;yy<y1;yy++){
        u32 inset=0,dist=yy-y0;
        if(y1-yy-1<dist)dist=y1-yy-1;
        if(dist<radius){
            u32 v=radius-dist;
            inset=radius-or_sqrt(radius*radius-v*v);
        }
        for(u32 xx=x0+inset;xx+inset<x1;xx++){
            u64 index=(u64)yy*boot->stride+xx;
            fb[index]=rgb(or_blend(rgb(fb[index]),tint,alpha));
        }
    }
}
static int or_round_hit(u32 x,u32 y,u32 x0,u32 y0,u32 x1,u32 y1,u32 r){
    if(x<x0||y<y0||x>=x1||y>=y1)return 0;
    if((x>=x0+r&&x+r<x1)||(y>=y0+r&&y+r<y1))return 1;
    u32 cx=x<x0+r?x0+r:x1-r-1,cy=y<y0+r?y0+r:y1-r-1;
    int dx=(int)x-(int)cx,dy=(int)y-(int)cy;
    return (u32)(dx*dx+dy*dy)<=r*r;
}
static void or_frost(u32 x,u32 y,u32 w,u32 h){
    /* Small block-filtered backdrop blur. It only runs inside glass cards;
     * controls remain crisp and the wallpaper is restored before each frame. */
    u32 x0=or_x(x),y0=or_y(y),x1=or_x(x+w),y1=or_y(y+h),r=or_x(18);
    if(x1>boot->width)x1=boot->width;if(y1>boot->height)y1=boot->height;
    volatile u32 *fb=(volatile u32*)boot->framebuffer;
    for(u32 by=y0;by<y1;by+=2)for(u32 bx=x0;bx<x1;bx+=2){
        u32 cx=bx+1,cy=by+1;if(cx>=x1)cx=bx;if(cy>=y1)cy=by;
        if(!or_round_hit(cx,cy,x0,y0,x1,y1,r))continue;
        u32 sr=0,sg=0,sb=0,count=0;
        for(int oy=-3;oy<=3;oy+=3)for(int ox=-3;ox<=3;ox+=3){
            int sx=(int)cx+ox,sy=(int)cy+oy;
            if(sx<0||sy<0||(u32)sx>=boot->width||(u32)sy>=boot->height)continue;
            u32 c=rgb(fb[(u64)(u32)sy*boot->stride+(u32)sx]);
            sr+=(c>>16)&255;sg+=(c>>8)&255;sb+=c&255;count++;
        }
        if(!count)continue;
        u32 avg=((sr/count)<<16)|((sg/count)<<8)|(sb/count);
        for(u32 yy=by;yy<by+2&&yy<y1;yy++)for(u32 xx=bx;xx<bx+2&&xx<x1;xx++)
            if(or_round_hit(xx,yy,x0,y0,x1,y1,r))fb[(u64)yy*boot->stride+xx]=rgb(avg);
    }
}
static void or_asset_icon(u32 id,u32 x,u32 y,u32 w,u32 h){
    if(!graphics_ready||id>=32||!w||!h)return;
    u32 x0=or_x(x),y0=or_y(y),x1=or_x(x+w),y1=or_y(y+h);
    if(x1>boot->width)x1=boot->width;if(y1>boot->height)y1=boot->height;
    if(x1<=x0||y1<=y0)return;
    u32 local=id<16?id:id-16;
    u32 source_x=(id<16?0U:256U)+(local%4U)*64U;
    u32 source_y=(local/4U)*64U;
    volatile u32 *fb=(volatile u32*)boot->framebuffer;
    for(u32 py=y0;py<y1;py++)for(u32 px=x0;px<x1;px++){
        u32 sx=source_x+(u32)((u64)(px-x0)*64U/(x1-x0));
        u32 sy=source_y+(u32)((u64)(py-y0)*64U/(y1-y0));
        if(sx>=source_x+64)sx=source_x+63;
        if(sy>=source_y+64)sy=source_y+63;
        const unsigned char *p=&orbis_icon_bgra[((u64)sy*512U+sx)*4U];
        u32 alpha=p[3];if(!alpha)continue;
        u32 color=((u32)p[2]<<16)|((u32)p[1]<<8)|p[0];
        u64 at=(u64)py*boot->stride+px;
        fb[at]=rgb(or_font_mix(rgb(fb[at]),color,alpha));
    }
}
#include "orbis_design.h"
static void or_app_icon(u32 x,u32 y,u32 k){
    static const u32 icons[6]={OR_ICON_FILES,OR_ICON_TERMINAL,OR_ICON_APPS,
                               OR_ICON_SETTINGS,OR_ICON_INFO_APP,OR_ICON_ACCOUNT};
    if(k<6)or_asset_icon(icons[k],x+5,y,63,63);
}
static void or_logo(u32 y){
    or_disc(477,y+9,24,0x202e3b);
    or_disc(477,y+9,16,0xe6e5de);
    or_label(521,y,"ORBIS OS",OR_WHITE);
}
static void or_mask(u32 x,u32 y){
    for(u32 i=0;i<or_pin_len;i++)or_disc(x+i*16,y,3,OR_WHITE);
}
static void or_button(u32 x,u32 y,u32 w,const char *label){
    or_glass(x,y+3,w,44,0x0a2c6e,100); /* soft drop shadow */
    or_glass(x,y,w,44,OR_BLUE,252);
    or_frame(x,y,w,44,0x6bb7ff);
    or_label(x+w/2-42,y+12,label,OR_WHITE);
}
static void or_panel(u32 y,const char *heading){
    or_glass(350,y+8,580,427,0x06101b,90);
    or_glass(355,y,570,425,OR_PANEL,219);
    or_frame(355,y,570,425,OR_EDGE);
    or_center(y+27,heading,OR_WHITE);
}
static void or_splash_screen(void){
    or_shadow(369,236,545,252);
    or_frost(369,236,545,252);
    or_glass(369,236,545,252,ORBIS_GLASS,112);
    or_brand_ring(558,332,36);
    or_text_display_at(or_x(613),or_y(307),"ORBIS OS",OR_WHITE);
    or_body_center(387,"Starting ORBIS OS",OR_WHITE);
    or_disc(632,450,18,0x344458);
    or_disc(632,450,14,0x172439);
    or_disc(638,439,5,ORBIS_ACCENT);
    or_body_center(607,"A more open tomorrow.",ORBIS_LIGHT);
}
static void or_setup_screen(void){
    /* Modern first-boot card with typographic hierarchy and warm brand. */
    or_brand_ring(91,162,30);
    or_text_display_at(or_x(134),or_y(134),"ORBIS OS",OR_WHITE);
    or_label(54,205,"A  B R I G H T E R",ORBIS_LIGHT);
    or_label(54,232,"C O M P U T I N G   T O M O R R O W",ORBIS_LIGHT);
    or_rect(54,328,46,3,ORBIS_ACCENT);
    or_label(54,350,"Modern. Focused. Human.",OR_WHITE);
    or_label(54,390,"ORBIS OS is a modern operating",OR_GRAY);
    or_label(54,417,"system designed for clarity,",OR_GRAY);
    or_label(54,444,"productivity and a more",OR_GRAY);
    or_label(54,471,"beautiful digital life.",OR_GRAY);
    or_label(1038,151,"S A M E   W O R L D .",0x39414a);
    or_label(1038,181,"A  B R I G H T E R",0x39414a);
    or_label(1038,208,"P E R S P E C T I V E .",0x39414a);
    or_rect(1038,240,37,3,ORBIS_ACCENT);
    or_shadow(355,123,570,483);
    or_frost(355,123,570,483);
    or_glass(355,123,570,483,ORBIS_GLASS,139);
    or_frame(355,123,570,483,0x56657a);
    or_brand_ring(640,167,23);
    or_heading_center(208,"Welcome to ORBIS OS",OR_WHITE);
    or_icon_monitor(399,294,OR_WHITE);
    or_label(430,291,"First boot - local preview setup",OR_WHITE);
    or_icon_globe(399,329,OR_WHITE);
    or_label(430,326,"Region: Brazil",OR_GRAY);
    or_rect(542,328,1,21,0x7a7582);
    or_label(570,326,"Language: English",OR_GRAY);
    or_icon_keyboard(399,361,OR_WHITE);
    or_label(430,358,"Keyboard: US",OR_GRAY);
    or_rect(542,360,1,21,0x7a7582);
    or_icon_clock(568,359,OR_WHITE);
    or_label(598,358,"Timezone: not configured",OR_GRAY);
    or_rect(397,391,480,1,0x7a7582);
    or_label(401,405,or_state==OR_SETUP_NAME?
       "Step 1: Display name":"Step 2: Demo PIN (4-16 chars)",OR_WHITE);
    or_field(405,436,462,44,or_state==OR_SETUP_NAME&&!or_name_len?
             "Enter your display name...":0,1,or_state==OR_SETUP_PIN);
    if(or_state==OR_SETUP_NAME)or_label(456,447,or_name,OR_WHITE);
    else or_mask(468,460);
    or_primary(405,494,462,"Continue");
    or_info_bar(453,557,"Volatile account. Not a secure login yet.");
}
static void or_login_screen(void){
    or_shadow(355,145,570,442);
    or_frost(355,145,570,442);
    or_glass(355,145,570,442,ORBIS_GLASS,146);
    or_frame(355,145,570,442,0x56657a);
    or_brand_ring(640,188,26);
    or_heading_center(232,"Sign in to ORBIS OS",OR_WHITE);
    or_disc(640,325,37,0x8394b5);
    or_disc(640,313,14,0xe7edf8);
    or_disc(640,344,21,0xd9e5f5);
    or_body_center(367,or_name[0]?or_name:"Local user",OR_WHITE);
    or_field(415,405,450,49,or_pin_len?0:"Enter your demo PIN...",1,1);
    if(or_pin_len)or_mask(520,429);
    or_primary(415,475,450,"Sign In");
    or_label(438,545,"G: Guest session",ORBIS_LIGHT);
    or_label(675,545,"Enter: Sign in",OR_GRAY);
    if(or_attempts)or_label(436,566,"Incorrect PIN",0xffb7a7);
    if(timer_ticks<or_lock_until)or_label(600,566,"Temporarily locked",0xffb7a7);
}
static void or_small_dock_icon(u32 x,u32 k){
    static const u32 icons[4]={OR_ICON_FILES,OR_ICON_TERMINAL,OR_ICON_APPS,OR_ICON_SETTINGS};
    if(k<4)or_asset_icon(icons[k],x+1,681,35,35);
}
static void or_bar(void){
    /* Floating dock instead of a full-width taskbar. */
    or_shadow(390,671,500,45);
    or_frost(390,671,500,45);
    or_glass(390,671,500,45,0x101722,174);
    or_frame(390,671,500,45,0x46556a);
    or_asset_icon(OR_ICON_BRAND,407,677,32,32);
    or_rect(454,681,1,25,0x526176);
    for(u32 i=0;i<4;i++)or_small_dock_icon(565+i*47,i);
    or_rect(767,681,1,25,0x526176);
    or_label(793,683,"PREVIEW",OR_GRAY);
}
static void or_desktop_screen(void){
    or_asset_icon(OR_ICON_DESKTOP,49,31,64,64);
    or_label(53,99,"Home",OR_WHITE);
    or_bar();
    or_shadow(72,184,336,421);
    or_frost(72,184,336,421);
    or_glass(72,184,336,421,OR_PANEL,151);
    or_frame(72,184,336,421,0x46556a);
    or_label(91,203,"ORBIS APPS",OR_WHITE);
    or_glass(91,234,300,33,0x394451,174);
    or_label(106,238,"Search apps...",OR_GRAY);
    for(u32 k=0;k<6;k++){
        u32 x=101+(k%3)*99,y=304+(k/3)*102;
        or_app_icon(x,y,k);
        const char *labels[6]={"Files","Terminal","Apps","Settings","Info","Account"};
        or_label(x,y+75,labels[k],OR_WHITE);
    }
    or_label(92,525,or_guest?"GUEST DEMO SESSION":or_name,OR_WHITE);
    or_label(92,568,"F files  S settings  L logout",OR_GRAY);
    or_shadow(457,174,764,432);
    or_frost(457,174,764,432);
    or_glass(457,174,764,432,OR_DARK,171);
    or_frame(457,174,764,432,0x46556a);
    or_glass(457,174,764,47,0x111b29,218);
    or_disc(482,197,6,0xec6b70);or_disc(502,197,6,0xe9ba5e);
    or_disc(522,197,6,0x67c98a);
    or_glass(854,180,299,33,0x354151,152);
    or_label(871,184,or_state==OR_FILES?"Search files...":"Search settings...",OR_GRAY);
    if(or_state==OR_FILES){
        or_label(551,187,"Files | Home",OR_WHITE);
        or_label(480,258,"Home",OR_GRAY);or_label(480,297,"Documents",OR_GRAY);
        or_label(480,336,"Downloads",OR_GRAY);or_label(480,375,"Pictures",OR_GRAY);
        or_rect(630,242,1,335,OR_EDGE);
        static const char *names[6]={"Desktop","Documents","Pictures","Music","Videos","Downloads"};
        static const u32 icons[6]={OR_ICON_DESKTOP,OR_ICON_DOCUMENTS,OR_ICON_PICTURES,
                                   OR_ICON_MUSIC,OR_ICON_VIDEOS,OR_ICON_DOWNLOADS};
        for(u32 k=0;k<6;k++){
            u32 x=658+(k%3)*180,y=253+(k/3)*149;
            or_asset_icon(icons[k],x+17,y+2,78,78);
            or_label(x,y+89,names[k],OR_WHITE);
        }
        or_label(651,568,"Sample folders - live FAT16 browsing follows",OR_GRAY);
    }else if(or_state==OR_SETTINGS){
        or_label(551,187,"Settings | Accounts",OR_WHITE);
        or_label(491,252,or_guest?"Guest Session":or_name,OR_WHITE);
        or_glass(492,298,692,53,0x3d4754,218);or_frame(492,298,692,53,0x6e7d8e);
        or_label(508,317,"Password & Security: volatile demo",OR_WHITE);
        or_glass(492,378,692,53,0x3d4754,218);or_frame(492,378,692,53,0x6e7d8e);
        or_label(508,397,"Sign-in options: PIN only",OR_WHITE);
        or_glass(492,458,692,53,0x3d4754,218);or_frame(492,458,692,53,0x6e7d8e);
        or_label(508,477,"Switch user: L",OR_WHITE);
    }else{
        or_label(551,187,"ORBIS OS | Desktop",OR_WHITE);
        or_label(503,268,"Welcome to the ORBIS OS native desktop.",OR_WHITE);
        or_label(503,321,"Warm dawn theme | dark glass-like panels",OR_GRAY);
        or_label(503,362,"Files: F   Settings: S   Logout: L",OR_WHITE);
        or_label(503,422,"Terminal and external apps: future milestone",OR_GRAY);
        or_label(503,468,"Keyboard input | GOP framebuffer",OR_GRAY);
    }
}
static void or_redraw_entry(void){
    or_entry_dirty=0;
    if(or_state==OR_SETUP_NAME||or_state==OR_SETUP_PIN){
        or_field(405,436,462,44,or_state==OR_SETUP_NAME&&!or_name_len?
                 "Enter your display name...":0,1,or_state==OR_SETUP_PIN);
        if(or_state==OR_SETUP_NAME)or_label(456,447,or_name,OR_WHITE);
        else or_mask(468,460);
    }else if(or_state==OR_LOGIN){
        or_field(415,405,450,49,or_pin_len?0:"Enter your demo PIN...",1,1);
        if(or_pin_len)or_mask(520,429);
    }
}
static int or_mouse_available,or_mouse_saved_valid;
static void or_mouse_draw(void);
static void or_draw(void){
    or_mouse_saved_valid=0;
    or_dirty=0;or_entry_dirty=0;
    if(!graphics_ready)return;
    or_wallpaper();
    if(or_state==OR_SPLASH)or_splash_screen();
    else if(or_state==OR_SETUP_NAME||or_state==OR_SETUP_PIN)or_setup_screen();
    else if(or_state==OR_LOGIN)or_login_screen();
    else or_desktop_screen();
    or_mouse_draw();
}
static u64 or_demo_hash(const char *p,u32 n){
    /* DEMO-ONLY FNV variant; not password KDF or persistence. */
    u64 v=14695981039346656037ULL^or_salt;
    for(u32 i=0;i<n;i++)v=(v^(u8)p[i])*1099511628211ULL;
    return v^(v>>29)^or_salt;
}
static void or_pin_clear(void){
    volatile char *p=(volatile char*)or_pin;
    for(u32 i=0;i<sizeof(or_pin);i++)p[i]=0;
    or_pin_len=0;
}
static void or_goto(u32 target,const char *log){
    or_state=target;or_dirty=1;
    if(log)serial(log);
}
static void or_input(int ch){
    if(or_state==OR_SPLASH)return;
    if(or_state==OR_SETUP_NAME){
        int edited=0;
        if(ch==8){if(or_name_len){or_name[--or_name_len]=0;edited=1;}}
        else if(ch=='\n'||ch=='\r'){
            if(or_name_len>=2){or_goto(OR_SETUP_PIN,"ORBIS_SETUP_PIN_READY\r\n");return;}
        }else if(ch>=32&&ch<=126&&or_name_len<20){
            or_name[or_name_len++]=(char)ch;or_name[or_name_len]=0;edited=1;
        }
        if(edited)or_entry_dirty=1;
        return;
    }
    if(or_state==OR_SETUP_PIN||or_state==OR_LOGIN){
        int edited=0,submitted=0;
        if(ch==8){if(or_pin_len){or_pin[--or_pin_len]=0;edited=1;}}
        else if(ch=='\n'||ch=='\r'){
            submitted=1;
            if(or_state==OR_SETUP_PIN&&or_pin_len>=4){
                or_salt=(timer_ticks<<32)^((u64)boot->framebuffer<<9)^0x8f376ab21987ULL;
                or_hash=or_demo_hash(or_pin,or_pin_len);
                or_pin_clear();
                serial("ORBIS_VOLATILE_USER_CREATED\r\n");
                or_goto(OR_LOGIN,"ORBIS_LOGIN_READY\r\n");return;
            }
            if(or_state==OR_LOGIN&&timer_ticks>=or_lock_until){
                u64 candidate=or_demo_hash(or_pin,or_pin_len);
                int valid=or_pin_len&&candidate==or_hash;
                or_pin_clear();
                if(valid){
                    or_attempts=0;or_guest=0;
                    or_goto(OR_DESKTOP,"ORBIS_DESKTOP_READY user=local\r\n");return;
                }
                or_attempts++;
                serial("ORBIS_LOGIN_DENIED\r\n");
                if(or_attempts>=5){
                    or_attempts=0;or_lock_until=timer_ticks+3000;
                    serial("ORBIS_LOGIN_LOCKED\r\n");
                }
            }
        }else if((ch=='g'||ch=='G')&&or_state==OR_LOGIN&&or_pin_len==0){
            or_guest=1;
            or_goto(OR_DESKTOP,"ORBIS_DESKTOP_READY user=guest\r\n");return;
        }else if(ch>=32&&ch<=126&&or_pin_len<16)
            {or_pin[or_pin_len++]=(char)ch;edited=1;}
        if(submitted)or_dirty=1;
        else if(edited)or_entry_dirty=1;
        return;
    }
    if(ch=='l'||ch=='L'){
        or_pin_clear();
        or_goto(OR_LOGIN,"ORBIS_LOGOUT_OK\r\nORBIS_LOGIN_READY\r\n");
    }else if(ch=='f'||ch=='F')or_goto(OR_FILES,"ORBIS_FILES_READY\r\n");
    else if(ch=='s'||ch=='S')or_goto(OR_SETTINGS,"ORBIS_SETTINGS_READY\r\n");
    else if(ch=='m'||ch=='M')or_goto(OR_DESKTOP,"ORBIS_DESKTOP_READY menu\r\n");
}
static void or_status(const char *cmd){
    if(cmd[0]=='s'&&cmd[1]=='t'&&cmd[2]=='a'&&cmd[3]=='t'&&
       cmd[4]=='u'&&cmd[5]=='s'&&cmd[6]==0){
        serial("ORBIS_STATUS state=");
        const char *s=or_state==OR_SPLASH?"splash":or_state==OR_SETUP_NAME?"setup_name":
          or_state==OR_SETUP_PIN?"setup_pin":or_state==OR_LOGIN?"login":
          or_state==OR_DESKTOP?"desktop":or_state==OR_FILES?"files":"settings";
        serial(s);
        serial(" storage=volatile security=preview framebuffer=");
        serial(graphics_ready?"ready\r\n":"missing\r\n");
    }
}
/* Minimal IRQ12 i8042/PS2 mouse driver for GUI-only QEMU builds.
 * Cursor is saved/restored directly from the GOP framebuffer.
 * No interrupts, USB, touchpad gestures, multitouch or device hotplug.
 */
#define OR_CURSOR_W 16
#define OR_CURSOR_H 22
static u32 or_mouse_x,or_mouse_y,or_mouse_count;
static u8 or_mouse_packet[3],or_mouse_buttons;
static u32 or_mouse_under[OR_CURSOR_W*OR_CURSOR_H];
static const char or_cursor_shape[OR_CURSOR_H][OR_CURSOR_W+1]={
    "X", "XX", "XWX", "XWWX", "XWWWX", "XWWWWX",
    "XWWWWWX", "XWWWWWWX", "XWWWWWWWX", "XWWWWWWWWX",
    "XWWWWWWWWWX", "XWWWWWXXXXXX", "XWWWX", "XWWXWX",
    "XWX XWX", "XX  XWX", "X    XWX", "     XWX",
    "      XWX", "      XWX", "       XX", ""
};
static void or_mouse_restore(void){
    if(!or_mouse_saved_valid)return;
    volatile u32 *fb=(volatile u32*)boot->framebuffer;
    for(u32 dy=0;dy<OR_CURSOR_H;dy++)for(u32 dx=0;dx<OR_CURSOR_W;dx++){
        u32 x=or_mouse_x+dx,y=or_mouse_y+dy;
        if(x<boot->width && y<boot->height)
            fb[(u64)y*boot->stride+x]=or_mouse_under[dy*OR_CURSOR_W+dx];
    }
    or_mouse_saved_valid=0;
}
static void or_mouse_draw(void){
    if(!or_mouse_available || !graphics_ready)return;
    volatile u32 *fb=(volatile u32*)boot->framebuffer;
    for(u32 dy=0;dy<OR_CURSOR_H;dy++)for(u32 dx=0;dx<OR_CURSOR_W;dx++){
        u32 x=or_mouse_x+dx,y=or_mouse_y+dy;
        if(x>=boot->width||y>=boot->height)continue;
        u64 off=(u64)y*boot->stride+x;
        or_mouse_under[dy*OR_CURSOR_W+dx]=fb[off];
        char pixel=or_cursor_shape[dy][dx];
        if(pixel=='X')fb[off]=rgb(0x111820);
        else if(pixel=='W')fb[off]=rgb(0xf7f9fc);
    }
    or_mouse_saved_valid=1;
}
static void or_mouse_click(void){
    u32 x=(u32)((u64)or_mouse_x*1280/boot->width);
    u32 y=(u32)((u64)or_mouse_y*720/boot->height);
    if(or_state==OR_SPLASH)return;
    if((or_state==OR_SETUP_NAME||or_state==OR_SETUP_PIN)
       &&x>=405&&x<=867&&y>=490&&y<=542){
        or_input('\n');return;
    }
    if(or_state==OR_LOGIN){
        if(x>=415&&x<=865&&y>=474&&y<=518)or_input('\n');
        else if(x>=415&&x<=680&&y>=535&&y<=572&&or_pin_len==0)
            or_input('g');
        return;
    }
    if(x>=90&&x<=397&&y>=557&&y<=606){or_input('l');return;}
    if(x>=100&&x<=188&&y>=299&&y<=380){or_input('f');return;}
    if(x>=100&&x<=188&&y>=405&&y<=487){or_input('s');return;}
    if(((x>=390&&x<=458)||x<=100)&&y>=670){or_input('m');return;}
    if(x>=1170&&y>=170&&y<=221){or_input('m');return;}
}
static void or_mouse_feed(u8 data){
    if(!or_mouse_available)return;
    if(!or_mouse_count && !(data&8U))return;
    or_mouse_packet[or_mouse_count++]=data;
    if(or_mouse_count!=3)return;
    or_mouse_count=0;
    if(or_mouse_packet[0]&0xc0U)return;
    int dx=(int)(signed char)or_mouse_packet[1];
    int dy=(int)(signed char)or_mouse_packet[2];
    or_mouse_restore();
    int nx=(int)or_mouse_x+dx,ny=(int)or_mouse_y-dy;
    if(nx<0)nx=0;if(ny<0)ny=0;
    if(nx>=(int)boot->width)nx=(int)boot->width-1;
    if(ny>=(int)boot->height)ny=(int)boot->height-1;
    or_mouse_x=(u32)nx;or_mouse_y=(u32)ny;
    int newly_clicked=(or_mouse_packet[0]&1U)&&!(or_mouse_buttons&1U);
    or_mouse_buttons=or_mouse_packet[0]&7U;
    if(newly_clicked)or_mouse_click();
    if(or_dirty)or_draw();
    else or_mouse_draw();
}
static int or_mouse_aux_ack(u8 value){
    if(!ps2_wait())return 0;
    outb(0x64,0xd4);
    if(!ps2_wait())return 0;
    outb(0x60,value);
    for(u32 i=0;i<200000;i++){
        u8 status=inb(0x64);
        if(status&1U){
            u8 reply=inb(0x60);
            if((status&0x20U)&&reply==0xfa)return 1;
            if((status&0x20U)&&reply==0xfe)return 0;
        }
        __asm__ volatile("pause");
    }
    return 0;
}
static int or_mouse_init(void){
    /* PS2 port 2, polled: no production IRQ routing or composite driver. */
    for(u32 i=0;i<16&&(inb(0x64)&1);i++)(void)inb(0x60);
    if(!ps2_wait())return 0;
    outb(0x64,0xa8); /* enable auxiliary port */
    if(!ps2_wait())return 0;
    outb(0x64,0x20); /* read controller configuration */
    u8 cfg=0;int got=0;
    for(u32 i=0;i<100000;i++)if(inb(0x64)&1){cfg=inb(0x60);got=1;break;}
    if(!got)return 0;
    cfg=(cfg&~(0x20U|0x02U))|0x40U;
    if(!ps2_wait())return 0;
    outb(0x64,0x60);
    if(!ps2_wait())return 0;
    outb(0x60,cfg);
    /* Ask for the highest standard PS/2 reporting rate. If an older device
     * rejects it, streaming still starts at its default rate. */
    int high_rate=or_mouse_aux_ack(0xf3)&&or_mouse_aux_ack(200);
    if(or_mouse_aux_ack(0xf4)){ /* Enable mouse stream. */
                or_mouse_x=boot->width/2;or_mouse_y=boot->height/2;
                /* IRQ12: unmask AUX controller, slave PIC IRQ4 and master cascade.
                 * The interrupt reads bytes into a ring and wakes the HLT loop. */
                if(!ps2_wait())return 0;
                outb(0x64,0x60);
                if(!ps2_wait())return 0;
                outb(0x60,cfg|0x02U);
                or_ps2_rd=or_ps2_wr=0;
                outb(0xa1,inb(0xa1)&~0x10U);
                outb(0x21,inb(0x21)&~0x04U);
                or_mouse_available=1;
                serial(high_rate?"ORBIS_MOUSE_RATE_HZ=200\r\n":"ORBIS_MOUSE_RATE_HZ=default\r\n");
                serial("ORBIS_MOUSE_IRQ12_READY\r\nORBIS_MOUSE_READY\r\n");
                return 1;
    }
    serial("ORBIS_MOUSE_UNAVAILABLE\r\n");
    return 0;
}
__attribute__((noreturn)) static void orbis_gui_run(void){
    if(!graphics_ready){
        serial("ORBIS_GUI_UNAVAILABLE: GOP framebuffer required\r\n");
        for(;;)__asm__ volatile("sti;hlt");
    }
    or_state=OR_SPLASH;or_splash_tick=timer_ticks;or_dirty=1;
    (void)or_mouse_init();
    serial("ORBIS_SPLASH_READY\r\n");
    char line[32]={0};u32 used=0;
    for(;;){
        __asm__ volatile("cli":::"memory");
        /* Hardware IRQ12 queues AUX bytes; never repaint with interrupts
         * disabled. Keyboard is still polled but cannot steal mouse bytes. */
        int c=0;
        int mouse_pending=or_ps2_rd!=or_ps2_wr;
        u8 mouse_byte=0;
        if(mouse_pending){
            mouse_byte=or_ps2_ring[or_ps2_rd];
            or_ps2_rd=(or_ps2_rd+1)&127U;
        }
        u8 ps2status=inb(0x64);
        if((ps2status&1U) && !(ps2status&0x20U))c=keyboard_char();
        int serial_byte=(inb(0x3fd)&1)?inb(0x3f8):0;
        __asm__ volatile("sti":::"memory");
        if(mouse_pending)or_mouse_feed(mouse_byte);
        if(serial_byte=='\r'||serial_byte=='\n'){
            line[used]=0;or_status(line);used=0;
        }else if(serial_byte>=32&&serial_byte<=126&&used<sizeof(line)-1)
            line[used++]=(char)serial_byte;
        if(or_state==OR_SPLASH&&timer_ticks-or_splash_tick>=125)
            or_goto(OR_SETUP_NAME,"ORBIS_SETUP_READY\r\n");
        if(c)or_input(c);
        if(or_dirty)or_draw();
        else if(or_entry_dirty){
            or_mouse_restore();
            or_redraw_entry();
            or_mouse_draw();
        }
        /* Recheck the ring with interrupts masked, then enable+sleep as one
         * sequence. This drains queued packets immediately and closes the
         * lost-wakeup window between the empty test and HLT. */
        if(!c&&!serial_byte){
            __asm__ volatile("cli":::"memory");
            int mouse_empty=or_ps2_rd==or_ps2_wr;
            if(mouse_empty)__asm__ volatile("sti;hlt":::"memory");
            else __asm__ volatile("sti":::"memory");
        }
    }
}
#endif
