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
    /* Warm anodized orbit mark, scaled on framebuffer, not a TTF glyph. */
    or_disc(centerx+2,centery+4,size+4,0x0e1926);
    or_disc(centerx,centery,size+2,0x653a45);
    or_disc(centerx,centery,size,ORBIS_PEACH);
    or_disc(centerx-2,centery-2,size-4,ORBIS_ACCENT);
    or_disc(centerx,centery,size-10,0x142238);
    or_disc(centerx-3,centery-4,size-14,0x1c3146);
    or_disc(centerx-size/3,centery-size/3,size/6,0xffc38b);
}
static void or_info_bar(u32 x,u32 y,const char *text){
    or_disc(x+9,y+10,9,ORBIS_LIGHT);
    or_disc(x+9,y+10,7,0x374250);
    or_label(x+6,y+1,"i",ORBIS_LIGHT);
    or_label(x+29,y,text,0xd3d8e3);
}
static void or_field(u32 x,u32 y,u32 w,u32 h,const char *placeholder,int focused){
    or_glass(x,y,w,h,ORBIS_SURFACE,228);
    or_frame(x,y,w,h,focused?0x769ac7:0x51627a);
    if(placeholder)or_label(x+18,y+14,placeholder,0xb8c6d6);
}
static void or_primary(u32 x,u32 y,u32 w,const char *s){
    /* Two-step gradient under a softened blue border. */
    or_glass(x+1,y+5,w,44,0x0e1728,104);
    or_glass(x,y,w,44,0x267af5,245);
    or_rect(x+9,y+1,w-18,3,0x6eafff);
    or_frame(x,y,w,44,0x6baafb);
    u32 t=or_type_width(s,0);
    if(w>t)or_label(x+(w-t)/2,y+11,s,0xffffff);
}
#endif
