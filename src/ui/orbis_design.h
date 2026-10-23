/* ORBIS Design System v2 — native software primitives, NO browser/Qt.
 * Conceptual Orbis Sans: the atlas is a locally rasterized substitute.
 * All drawing is clipped by the physical GOP dimensions.
 */
#ifndef ORBIS_DESIGN_V2_H
#define ORBIS_DESIGN_V2_H
#define ORBIS_ACCENT 0xff963fU
#define ORBIS_PEACH 0xffb77bU
#define ORBIS_LIGHT 0xffe6cbU
#define ORBIS_GLASS 0x101827U
#define ORBIS_SURFACE 0x253144U
#define ORBIS_BORDER 0x53657cU
static u32 or_type_width(const char *s,int display){
    const u8 *width=display?orbis_display_advance:orbis_body_advance;
    u32 n=0;
    while(*s && n<600){
        u8 c=(u8)*s++;
        n+=width[c>=32&&c<=126?c-32:31];
    }
    return n;
}
static void or_text_display_at(u32 px,u32 py,const char *s,u32 fg){
    if(!graphics_ready)return;
    volatile u32 *fb=(volatile u32*)boot->framebuffer;
    while(*s && px+ORBIS_DISPLAY_W<boot->width){
        u8 ch=(u8)*s++;
        if(ch<32||ch>126)ch='?';
        const u8 *a=orbis_display_alpha[ch-32];
        for(u32 dy=0;dy<ORBIS_DISPLAY_H;dy++){
            u32 y=py+dy;if(y>=boot->height)continue;
            for(u32 dx=0;dx<ORBIS_DISPLAY_W;dx++){
                u32 x=px+dx;if(x>=boot->width)continue;
                u8 alpha=a[dy*ORBIS_DISPLAY_W+dx];
                if(!alpha)continue;
                u64 at=(u64)y*boot->stride+x;
                fb[at]=rgb(or_font_mix(rgb(fb[at]),fg,alpha));
            }
        }
        px+=orbis_display_advance[ch-32];
    }
}
static void or_body_center(u32 y,const char *s,u32 color){
    u32 w=or_type_width(s,0);
    u32 x=boot->width>w?(boot->width-w)/2:0;
    or_label((u32)((u64)x*1280/boot->width),y,s,color);
}
static void or_heading_center(u32 baseline,const char *text,u32 color){
    u32 length=or_type_width(text,1);
    u32 x=boot->width>length?(boot->width-length)/2:0;
    or_text_display_at(x,or_y(baseline),text,color);
}
static void or_brand_ring(u32 centerx,u32 centery,u32 size){
    /* Antialiased warm orbit mark. It is rendered from geometry rather than
     * stacked opaque discs, which keeps it clean at GOP resolution. */
    if(!graphics_ready)return;
    int cx=(int)or_x(centerx),cy=(int)or_y(centery);
    int outer=(int)or_x(size),inner=outer-(int)or_x(size>24?12:10);
    if(inner<2)inner=2;
    volatile u32 *fb=(volatile u32*)boot->framebuffer;
    for(int dy=-outer-6;dy<=outer+6;dy++)for(int dx=-outer-6;dx<=outer+6;dx++){
        int px=cx+dx,py=cy+dy;
        if(px<0||py<0||(u32)px>=boot->width||(u32)py>=boot->height)continue;
        u32 d=or_sqrt((u32)(dx*dx+dy*dy));
        u32 alpha=0,color=0;
        if(d>=(u32)(outer+2)&&d<=(u32)(outer+6)){
            alpha=(outer+6-(int)d)*22U;color=0x07111e;
        }else if(d>=(u32)(inner-1)&&d<=(u32)(outer+1)){
            if(d<(u32)inner)alpha=(d-(u32)(inner-1))*255U;
            else if(d>(u32)outer)alpha=(outer+1-(int)d)*150U;
            else alpha=255;
            u32 warm=(u32)(dy+outer)*255U/(u32)(outer*2+1);
            color=or_blend(ORBIS_PEACH,dx<0?ORBIS_ACCENT:0xff6f55,warm);
            if(dx<-outer/3&&dy<-outer/4)color=or_blend(color,0xffd09a,120);
        }
        if(alpha){
            u64 at=(u64)(u32)py*boot->stride+(u32)px;
            fb[at]=rgb(or_blend(rgb(fb[at]),color,alpha>255?255:alpha));
        }
    }
}
static void or_icon_user(u32 x,u32 y,u32 c){
    or_disc(x+11,y+8,6,c);or_disc(x+11,y+8,4,ORBIS_SURFACE);
    or_frame(x+2,y+15,19,13,c);
}
static void or_icon_lock(u32 x,u32 y,u32 c){
    or_frame(x+4,y+10,16,15,c);
    or_disc(x+12,y+9,7,c);or_disc(x+12,y+9,5,ORBIS_SURFACE);
    or_rect(x+11,y+16,2,6,c);
}
static void or_icon_monitor(u32 x,u32 y,u32 c){
    or_frame(x,y,18,12,c);or_rect(x+8,y+12,2,5,c);or_rect(x+4,y+17,10,2,c);
}
static void or_icon_globe(u32 x,u32 y,u32 c){
    or_disc(x+9,y+9,9,c);or_disc(x+9,y+9,7,ORBIS_GLASS);
    or_rect(x+8,y,2,18,c);or_rect(x+1,y+8,16,2,c);
}
static void or_icon_keyboard(u32 x,u32 y,u32 c){
    or_frame(x,y+2,19,13,c);
    for(u32 row=0;row<2;row++)for(u32 col=0;col<4;col++)or_rect(x+3+col*4,y+5+row*4,2,2,c);
}
static void or_icon_clock(u32 x,u32 y,u32 c){
    or_disc(x+9,y+9,9,c);or_disc(x+9,y+9,7,ORBIS_GLASS);
    or_rect(x+8,y+4,2,6,c);or_rect(x+9,y+9,5,2,c);
}
static void or_info_bar(u32 x,u32 y,const char *text){
    or_disc(x+9,y+10,9,ORBIS_LIGHT);
    or_disc(x+9,y+10,7,0x374250);
    or_label(x+6,y+1,"i",ORBIS_LIGHT);
    or_label(x+29,y,text,0xd3d8e3);
}
static void or_shadow(u32 x,u32 y,u32 w,u32 h){
    /* Flat visual system: a single low-contrast ambient separation. */
    or_glass(x,y+4,w,h,0x030912,20);
}
static void or_gradient_round(u32 x,u32 y,u32 w,u32 h,u32 top,u32 bottom){
    u32 x0=or_x(x),y0=or_y(y),x1=or_x(x+w),y1=or_y(y+h),r=or_x(13);
    if(x1>boot->width)x1=boot->width;if(y1>boot->height)y1=boot->height;
    volatile u32 *fb=(volatile u32*)boot->framebuffer;
    u32 height=y1>y0?y1-y0:1;
    for(u32 yy=y0;yy<y1;yy++){
        u32 d=yy-y0,inset=0;
        if(y1-yy-1<d)d=y1-yy-1;
        if(d<r){u32 v=r-d;inset=r-or_sqrt(r*r-v*v);}
        u32 c=or_blend(top,bottom,(yy-y0)*255U/height);
        for(u32 xx=x0+inset;xx+inset<x1;xx++)fb[(u64)yy*boot->stride+xx]=rgb(c);
    }
}
static void or_field(u32 x,u32 y,u32 w,u32 h,const char *placeholder,int focused,int lock){
    or_glass(x,y,w,h,0x26354a,188);
    or_frame(x,y,w,h,focused?0x557aa7:0x3d4c60);
    if(lock)or_icon_lock(x+14,y+8,focused?0xd8e3f2:0x9ba9bc);
    else or_icon_user(x+14,y+8,focused?0xd8e3f2:0x9ba9bc);
    if(placeholder)or_label(x+51,y+11,placeholder,0xb8c6d6);
}
static void or_primary(u32 x,u32 y,u32 w,const char *s){
    or_gradient_round(x,y,w,44,0x3f91ff,0x1765df);
    u32 t=or_type_width(s,0);
    if(w>t)or_label(x+(w-t)/2-8,y+10,s,0xffffff);
    or_rect(x+w-43,y+21,17,2,0xffffff);
    for(u32 i=0;i<6;i++){
        or_rect(x+w-34+i,y+16+i,2,2,0xffffff);
        or_rect(x+w-34+i,y+26-i,2,2,0xffffff);
    }
}
#endif
