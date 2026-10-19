/* The menus' PNG art for the Mac: decoded by ImageIO, uploaded once as an
   RGBA texture (padded to a power of two) and handed to the texture binder
   as a borrowed texture. The pictures are read from the folder named by
   HALO_MAC_MENU_ART_DIR (port/assets/menus of a checkout, until the kit
   embeds them). */
#include "engine_menu_art.h"
#include "mac_gpu_info.h"
#include "../linux/include/input_prompts.h"
#include "../linux/src/port_config.h"
#include <ApplicationServices/ApplicationServices.h>
#include <OpenGL/gl.h>
#include <mach-o/dyld.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* mac_gpu_textures.c: the binder's shadow of a texture name, let go before the name is */
void mac_gpu_textures_forget(GLuint name);
#define MAXIMUM_ART 1024
/* name, scale_u, scale_v, width, height and failed are the picture's; variant_* the
   PlayStation pad's picture of the frame (its PNG's name with "@ps" before the
   extension), if it has one */
struct art { const void *texture; char *png; GLuint name; float scale_u, scale_v; long width, height; int failed;
    GLuint variant_name; float variant_scale_u, variant_scale_v; int variant_failed; int attempts; };
#define UPLOAD_ATTEMPTS 120
static struct art arts[MAXIMUM_ART];
static unsigned art_count;
void mac_menu_art_register(const void *texture, const char *png)
{
    struct art *art;
    if(art_count>=MAXIMUM_ART || !texture || !png)return;
    art=&arts[art_count];memset(art,0,sizeof(*art));
    art->png=(char *)malloc(strlen(png)+1);
    if(!art->png)return;
    strcpy(art->png,png);art->texture=texture;++art_count;
}
void mac_menu_art_forget(void)
{
    unsigned i;
    for(i=0;i<art_count;++i) {
        if(arts[i].name){mac_gpu_textures_forget(arts[i].name);glDeleteTextures(1,&arts[i].name);}
        if(arts[i].variant_name){mac_gpu_textures_forget(arts[i].variant_name);glDeleteTextures(1,&arts[i].variant_name);}
        free(arts[i].png);
    }
    memset(arts,0,sizeof(arts));art_count=0;
}
static unsigned long power_of_two(unsigned long value)
{ unsigned long result=1;while(result<value)result<<=1;return result; }
/* where the pictures are: HALO_MAC_MENU_ART_DIR, else the menus folder beside
   the executable (the kit's copy of port/assets/menus: ce/shell/...), else a
   source checkout's, from its root */
static const char *art_directory(void)
{
    static char found[1100];
    static int looked;
    const char *env=getenv("HALO_MAC_MENU_ART_DIR");
    if(env&&*env)return env;
    if(!looked) {
        char exe[1024],probe[1100];
        uint32_t size=sizeof(exe);
        struct stat info;
        char *slash;
        looked=1;
        strcpy(found,"port/assets/menus");
        if(_NSGetExecutablePath(exe,&size)==0&&(slash=strrchr(exe,'/'))!=NULL) {
            *slash=0;
            snprintf(probe,sizeof(probe),"%s/menus/ce",exe);
            if(stat(probe,&info)==0&&S_ISDIR(info.st_mode))snprintf(found,sizeof(found),"%s/menus",exe);
        }
    }
    return found;
}
/* port: the redraws are drawn for large displays (list_field is 2048x2048, 16 MB as a texture, when the picture
   it is drawn as is 320x322): on a 32 MB card one of them does not fit with the frame buffers and the
   game's own textures, and the card gave a scramble of other textures' pictures (the Choose Difficulty
   panel, after a level had been played). A picture goes up halved, in both directions, until it is at most
   1/16 of the card's memory, on a card of 128 MB or less. */
static void shrink_for_card(unsigned char **pixels,unsigned long *width,unsigned long *height)
{
    long vram=mac_gpu_info_vram_mb();
    unsigned long limit;
    /* (HALO_MAC_MENU_ART_VRAM_MB: another card's memory, to see a small card's pictures on a large one) */
    if(getenv("HALO_MAC_MENU_ART_VRAM_MB"))vram=atol(getenv("HALO_MAC_MENU_ART_VRAM_MB"));
    if(vram<=0||vram>128)return;
    limit=(unsigned long)vram*1024UL*1024UL/16UL;
    while(*width*(*height)*4UL>limit&&*width>1&&*height>1) {
        unsigned long w=*width,h=*height,nw=(w+1)/2,nh=(h+1)/2,x,y;
        unsigned char *half=(unsigned char *)malloc(nw*nh*4UL);
        if(!half)return;
        for(y=0;y<nh;++y)for(x=0;x<nw;++x) {
            unsigned long x1=2*x+1<w?2*x+1:2*x,y1=2*y+1<h?2*y+1:2*y,c,alpha=0;
            const unsigned char *p[4];
            p[0]=*pixels+(2*y*w+2*x)*4;p[1]=*pixels+(2*y*w+x1)*4;p[2]=*pixels+(y1*w+2*x)*4;p[3]=*pixels+(y1*w+x1)*4;
            /* (straight alpha: the colors are weighted by it, so that a clear texel's color does not bleed) */
            for(c=0;c<4;++c)alpha+=p[c][3];
            for(c=0;c<3;++c) {
                unsigned long sum=0,i;
                for(i=0;i<4;++i)sum+=(unsigned long)p[i][c]*p[i][3];
                half[(y*nw+x)*4+c]=alpha?(unsigned char)((sum+alpha/2)/alpha):(unsigned char)((p[0][c]+p[1][c]+p[2][c]+p[3][c]+2)/4);
            }
            half[(y*nw+x)*4+3]=(unsigned char)((alpha+2)/4);
        }
        free(*pixels);*pixels=half;*width=nw;*height=nh;
    }
}
/* RGBA bytes, top row first; NULL on failure */
/* port/linux/src/menu_pc_art.c: a frame of the player's own Halo install, if
   it has one of this PNG's picture and there is such an install */
unsigned char *halo_menus_pc_art(const char *png,long *width,long *height);
const char *halo_menus_pc_art_source(void);
static unsigned char *decode(const char *png,unsigned long *width,unsigned long *height,int variant)
{
    const char *directory=art_directory();
    if(!variant) {
        long pc_width=0,pc_height=0;
        unsigned char *pc=halo_menus_pc_art(png,&pc_width,&pc_height);
        if(pc) {
            if(getenv("HALO_MAC_MENU_DEBUG"))fprintf(stderr,"menus: %s is drawn from %s\n",png,halo_menus_pc_art_source());
            *width=(unsigned long)pc_width;*height=(unsigned long)pc_height;
            return pc;
        }
    }
    char path[1024];
    CFStringRef string;CFURLRef url;CGImageSourceRef source;CGImageRef image;
    CGColorSpaceRef space;CGContextRef context;unsigned char *pixels;
    unsigned long w,h,i;
    if(strlen(directory)+strlen(png)+2>sizeof(path))return NULL;
    snprintf(path,sizeof(path),"%s/%s",directory,png);
    string=CFStringCreateWithCString(NULL,path,kCFStringEncodingUTF8);
    if(!string)return NULL;
    url=CFURLCreateWithFileSystemPath(NULL,string,kCFURLPOSIXPathStyle,false);
    CFRelease(string);
    if(!url)return NULL;
    source=CGImageSourceCreateWithURL(url,NULL);CFRelease(url);
    if(!source)return NULL;
    image=CGImageSourceCreateImageAtIndex(source,0,NULL);CFRelease(source);
    if(!image)return NULL;
    w=CGImageGetWidth(image);h=CGImageGetHeight(image);
    if(!w||!h||w>4096||h>4096){CGImageRelease(image);return NULL;}
    pixels=(unsigned char *)calloc(w*h,4);
    space=CGColorSpaceCreateDeviceRGB();
    context=pixels&&space?CGBitmapContextCreate(pixels,w,h,8,w*4,space,
        kCGImageAlphaPremultipliedLast|kCGBitmapByteOrder32Big):NULL;
    if(space)CGColorSpaceRelease(space);
    if(!context){free(pixels);CGImageRelease(image);return NULL;}
    CGContextDrawImage(context,CGRectMake(0,0,(float)w,(float)h),image);
    CGContextRelease(context);CGImageRelease(image);
    /* straight alpha, as the game blends */
    for(i=0;i<w*h;++i) {
        unsigned char *p=pixels+i*4;
        if(p[3]&&p[3]!=255) {
            unsigned c;
            for(c=0;c<3;++c){unsigned v=((unsigned)p[c]*255U+p[3]/2U)/p[3];p[c]=(unsigned char)(v>255U?255U:v);}
        }
    }
    *width=w;*height=h;return pixels;
}
/* ImageIO's rows are top first; so are the game's texture coordinates */
static int upload(struct art *art,long width,long height,int variant)
{
    unsigned long w=0,h=0,pw,ph,y;unsigned char *pixels,*padded;
    GLint binding,unpack;GLuint name;GLenum error;
    char variant_png[1024];
    const char *png=art->png;
    size_t length=strlen(png);
    if(variant) {
        if(length<5||length+4>sizeof(variant_png)||strcmp(png+length-4,".png")){art->variant_failed=1;return 0;}
        memcpy(variant_png,png,length-4);strcpy(variant_png+length-4,"@ps.png");png=variant_png;
    }
    pixels=decode(png,&w,&h,variant);
    if(!pixels){
        if(variant)art->variant_failed=1;
        else {fprintf(stderr,"menus: could not read %s\n",png);art->failed=1;}
        return 0;
    }
    if(getenv("HALO_MAC_MENU_DEBUG")) {
        unsigned long i,opaque=0,visible=0;
        for(i=0;i<w*h;++i){if(pixels[i*4+3])++visible;if(pixels[i*4+3]==255)++opaque;}
        fprintf(stderr,"menus: %s is %lux%lu, %lu texels with alpha, %lu opaque, first %02x%02x%02x%02x\n",art->png,w,h,visible,opaque,
            pixels[0],pixels[1],pixels[2],pixels[3]);
    }
    shrink_for_card(&pixels,&w,&h);
    pw=power_of_two(w);ph=power_of_two(h);
    padded=(unsigned char *)calloc(pw*ph,4);
    if(!padded){free(pixels);if(variant)art->variant_failed=1;else art->failed=1;return 0;}
    for(y=0;y<h;++y)memcpy(padded+y*pw*4,pixels+y*w*4,w*4);
    free(pixels);
    /* port: an error an earlier call left (the renderer's, with the card's memory full) is not this
       upload's: it used to fail the picture for good, and the menu drew what the last draw had bound
       (a level's preview, in the panel of the difficulty screen, on a 32 MB card) */
    while(glGetError()!=GL_NO_ERROR){}
    glGetIntegerv(GL_TEXTURE_BINDING_2D,&binding);
    glGetIntegerv(GL_UNPACK_ALIGNMENT,&unpack);
    glGenTextures(1,&name);
    glBindTexture(GL_TEXTURE_2D,name);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,(GLsizei)pw,(GLsizei)ph,0,GL_RGBA,GL_UNSIGNED_BYTE,padded);
    glBindTexture(GL_TEXTURE_2D,(GLuint)binding);
    glPixelStorei(GL_UNPACK_ALIGNMENT,unpack);
    free(padded);
    error=glGetError();
    if(error!=GL_NO_ERROR) {
        glDeleteTextures(1,&name);
        /* (no memory is for now: tried again at the next draw, a few times, not failed for good) */
        if(error==GL_OUT_OF_MEMORY&&art->attempts<UPLOAD_ATTEMPTS){++art->attempts;return 0;}
        fprintf(stderr,"menus: %s: OpenGL error 0x%04x on upload\n",art->png,(unsigned)error);
        if(variant)art->variant_failed=1;else art->failed=1;return 0;
    }
    if(variant) {
        art->variant_name=name;
        art->variant_scale_u=(float)w/(float)pw;art->variant_scale_v=(float)h/(float)ph;
        return 1;
    }
    if(getenv("HALO_MAC_MENU_DEBUG"))fprintf(stderr,"menus: %s uploaded as GL texture %u, %lux%lu texels\n",art->png,(unsigned)name,pw,ph);
    art->name=name;art->width=width;art->height=height;
    /* the game's coordinates are 0 to 1 over the picture (the bitmap's own
       size is the picture's, whatever its texels): over the padded texture's
       part that holds it */
    art->scale_u=(float)w/(float)pw;
    art->scale_v=(float)h/(float)ph;
    return 1;
}
int mac_menu_art_texture(const void *texture, long width, long height,
    unsigned int *name, float scale[4])
{
    unsigned i;
    struct art *art=NULL;
    for(i=0;i<art_count;++i)if(arts[i].texture==texture){art=&arts[i];break;}
    if(!art)return 0;
    if(art->failed||width<=0||height<=0)return -1;
    /* the pad in use is a PlayStation pad (input.prompt_glyphs): its picture, if the frame has one */
    if(!art->variant_failed&&input_prompt_glyphs_playstation(config_string("input.prompt_glyphs"))) {
        if(!art->variant_name)upload(art,width,height,1);
        if(art->variant_name) {
            *name=(unsigned int)art->variant_name;
            scale[0]=art->variant_scale_u;scale[1]=art->variant_scale_v;scale[2]=scale[3]=1.0f;
            return 1;
        }
    }
    if(!art->name&&!upload(art,width,height,0)){
        /* (a picture that is not made yet draws nothing: -1 fails the draw; 0 would take the placeholder's) */
        return -1;
    }
    *name=(unsigned int)art->name;
    scale[0]=art->scale_u;scale[1]=art->scale_v;scale[2]=scale[3]=1.0f;
    return 1;
}
