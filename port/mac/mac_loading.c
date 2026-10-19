#include "mac_loading.h"
#include "mac_window.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__) && !defined(__LP64__)
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include "text_hires.h"
/* (the label's font: stb_truetype as text_hires.c has it, here for this file alone) */
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "../third_party/stb/stb_truetype.h"

#define MASK_WIDTH 128
#define MASK_HEIGHT 16
#define LOADING_PI 3.14159265358979
/* (the Xbox's band is the loading picture, which retail discs do not have, lit by the mask: a
   plain light here, dimmer than the mask itself, which the fade layer builds up on) */
#define BAND_LEVEL 0.16f

extern int config_boolean(const char *name);

static struct {
    int started;
    unsigned *base;
    unsigned base_width,base_height;
    GLuint feedback,mask,label;
    int label_ready;
    int feedback_ready;
    unsigned feedback_width,feedback_height;
    /* progress_bar.c's wobble_phase and last_t */
    float wobble,last;
} loading;

void mac_loading_start(const unsigned *pixels,unsigned width,unsigned height,unsigned pitch_pixels)
{
    unsigned y;
    GLuint label=loading.label;
    int label_ready=loading.label_ready;
    free(loading.base);
    memset(&loading,0,sizeof(loading));
    /* (the label's texture is made once) */
    loading.label=label;loading.label_ready=label_ready;
    if(pixels&&width&&height&&width<=4096&&height<=4096&&pitch_pixels>=width) {
        loading.base=(unsigned *)malloc((size_t)width*height*sizeof(unsigned));
        if(loading.base) {
            /* (rows from the bottom, as the framebuffer's) */
            for(y=0;y<height;++y)
                memcpy(loading.base+(size_t)y*width,pixels+(size_t)(height-1-y)*pitch_pixels,(size_t)width*sizeof(unsigned));
            loading.base_width=width;loading.base_height=height;
        }
    }
    loading.started=1;
}

/* the band's mask: progress_bar_create_mask_texture's intensity, a lozenge that is brightest in
   the middle of its width and height, over a faint level */
static void make_mask(void)
{
    unsigned char mask[MASK_WIDTH*MASK_HEIGHT];
    int x,y;
    for(y=0;y<MASK_HEIGHT;++y) {
        double glow=pow(sin(LOADING_PI*(y*(1.0/15.0))),0.75);
        for(x=0;x<MASK_WIDTH;++x) {
            double value=(sin(LOADING_PI*(x*(1.0/127.0)))*glow*(250.0/255.0)+5.0/255.0)*255.0;
            mask[y*MASK_WIDTH+x]=(unsigned char)(value<0?0:value>255?255:value);
        }
    }
    glGenTextures(1,&loading.mask);
    glBindTexture(GL_TEXTURE_2D,loading.mask);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    glTexImage2D(GL_TEXTURE_2D,0,GL_LUMINANCE8,MASK_WIDTH,MASK_HEIGHT,0,GL_LUMINANCE,GL_UNSIGNED_BYTE,mask);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    {
        /* (the Xbox's mask has a border of 5/255 outside it) */
        GLfloat border[4];
        border[0]=border[1]=border[2]=border[3]=5.0f/255.0f;
        glTexParameterfv(GL_TEXTURE_2D,GL_TEXTURE_BORDER_COLOR,border);
    }
}

static void make_feedback(unsigned width,unsigned height)
{
    glGenTextures(1,&loading.feedback);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,loading.feedback);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    if(loading.base&&loading.base_width==width&&loading.base_height==height)
        glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA8,(GLsizei)width,(GLsizei)height,0,GL_BGRA,
            GL_UNSIGNED_INT_8_8_8_8_REV,loading.base);
    else {
        /* port: the last picture was the game's own size (kept in its shape, centred, as the present
           step places it in a larger window) and the window is not: fitted into black of the window's */
        unsigned *fitted=(unsigned *)calloc((size_t)width*height,4);
        if(fitted&&loading.base&&loading.base_width&&loading.base_height) {
            unsigned fit_width=width,fit_height=(unsigned)((unsigned long long)width*loading.base_height/loading.base_width);
            unsigned left,bottom,x,y;
            if(fit_height>height){fit_height=height;fit_width=(unsigned)((unsigned long long)height*loading.base_width/loading.base_height);}
            if(!fit_width)fit_width=1;
            if(!fit_height)fit_height=1;
            left=(width-fit_width)/2;bottom=(height-fit_height)/2;
            for(y=0;y<fit_height;++y) {
                const unsigned *row=loading.base+(size_t)((2*(unsigned long long)y+1)*loading.base_height/(2*fit_height))*loading.base_width;
                unsigned *out=fitted+(size_t)(bottom+y)*width+left;
                for(x=0;x<fit_width;++x)out[x]=row[(2*(unsigned long long)x+1)*loading.base_width/(2*fit_width)];
            }
        }
        glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA8,(GLsizei)width,(GLsizei)height,0,GL_BGRA,
            GL_UNSIGNED_INT_8_8_8_8_REV,fitted);
        free(fitted);
    }
    loading.feedback_width=width;loading.feedback_height=height;
    free(loading.base);
    loading.base=NULL;
    loading.feedback_ready=1;
}

/* The indicator (mac.loading_indicator, on by default): "LOADING" in the project's Overpass (the font that
   draws the menus' text, taken from the fonts the build embeds for text_hires.c and rasterized once, here, into
   a texture), a ring of dots turning, and a thin bar of the progress, on a dark rounded backing so that they
   stand clear of the faded menu. All sizes are of the window's height over 480 lines. */
#define LABEL_WIDTH 512
#define LABEL_HEIGHT 128
#define LABEL_CAPS 64.0f
#define LABEL_BASELINE 96

static void make_label(void)
{
    unsigned char *bitmap;
    unsigned index;
    const struct text_hires_embedded *font=NULL;
    stbtt_fontinfo info;
    int x0,y0,x1,y1,pen=0,letter,total=0;
    float scale;
    static const char word[]="LOADING";
    loading.label_ready=1; /* (once: a font that cannot be read leaves the label out) */
    for(index=0;index<text_hires_embedded_count;++index)
        if(text_hires_embedded[index].file&&strstr(text_hires_embedded[index].file,"Overpass-900")){font=&text_hires_embedded[index];break;}
    if(!font)return;
    if(!stbtt_InitFont(&info,(const unsigned char *)font->data,stbtt_GetFontOffsetForIndex((const unsigned char *)font->data,0)))return;
    if(!stbtt_GetCodepointBox(&info,'H',&x0,&y0,&x1,&y1)||y1<=y0)return;
    scale=LABEL_CAPS/(float)(y1-y0);
    bitmap=(unsigned char *)calloc(LABEL_WIDTH,LABEL_HEIGHT);
    if(!bitmap)return;
    /* (the letters, spaced a little wider than the font's, centred in the texture: first measured, then drawn) */
    for(letter=0;letter<7;++letter) {
        int advance,bearing;
        stbtt_GetCodepointHMetrics(&info,word[letter],&advance,&bearing);
        total+=(int)(advance*scale+0.5f)+(letter<6?6:0);
    }
    pen=(LABEL_WIDTH-total)/2;
    if(pen<0)pen=0;
    for(letter=0;letter<7;++letter) {
        int advance,bearing,left,top,right,bottom,x,y;
        stbtt_GetCodepointHMetrics(&info,word[letter],&advance,&bearing);
        stbtt_GetCodepointBitmapBox(&info,word[letter],scale,scale,&left,&top,&right,&bottom);
        x=pen+left;y=LABEL_BASELINE+top;
        if(x>=0&&y>=0&&x+(right-left)<=LABEL_WIDTH&&y+(bottom-top)<=LABEL_HEIGHT)
            stbtt_MakeCodepointBitmap(&info,bitmap+(size_t)y*LABEL_WIDTH+x,right-left,bottom-top,LABEL_WIDTH,scale,scale,word[letter]);
        pen+=(int)(advance*scale+0.5f)+6;
    }
    glGenTextures(1,&loading.label);
    glBindTexture(GL_TEXTURE_2D,loading.label);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    glTexImage2D(GL_TEXTURE_2D,0,GL_ALPHA8,LABEL_WIDTH,LABEL_HEIGHT,0,GL_ALPHA,GL_UNSIGNED_BYTE,bitmap);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    free(bitmap);
}

/* a rounded rectangle (a fan), the corners eight steps each */
static void rounded_rectangle(float left,float bottom,float right,float top,float radius)
{
    int corner,step;
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f((left+right)*0.5f,(bottom+top)*0.5f);
    for(corner=0;corner<4;++corner) {
        float cx=(corner==0||corner==3)?right-radius:left+radius;
        float cy=(corner<2)?top-radius:bottom+radius;
        for(step=0;step<=8;++step) {
            float angle=(float)(LOADING_PI*0.5*((double)corner+(double)step/8.0));
            glVertex2f(cx+(float)cos(angle)*radius,cy+(float)sin(angle)*radius);
        }
    }
    glVertex2f(right,top);
    glEnd();
}

static void draw_indicator(float w,float h,float unit,float t)
{
    float label_scale=20.0f*unit/LABEL_CAPS,label_w=(float)LABEL_WIDTH*label_scale,label_h=(float)LABEL_HEIGHT*label_scale;
    float base=h*0.5f-57.0f*unit,dot_size=4.0f*unit;
    float centre_x=w*0.5f,centre_y=h*0.5f+14.0f*unit,radius=22.0f*unit;
    float bar_left=w*0.5f-120.0f*unit,bar_right=w*0.5f+120.0f*unit,bar_y=base-28.0f*unit,bar_h=3.0f*unit;
    float label_left=centre_x-label_w*0.5f,label_bottom=base-(float)(LABEL_HEIGHT-LABEL_BASELINE)*label_scale;
    int dot;
    glDisable(GL_TEXTURE_2D);glDisable(GL_TEXTURE_RECTANGLE_ARB);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    /* (the backing: dark, and not opaque, so that the faded menu shows through a little) */
    glColor4f(0.0f,0.02f,0.05f,0.72f);
    rounded_rectangle(centre_x-150.0f*unit,bar_y-16.0f*unit,centre_x+150.0f*unit,centre_y+radius+dot_size+18.0f*unit,16.0f*unit);
    if(!loading.label_ready)make_label();
    if(loading.label) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D,loading.label);
        glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
        glColor4f(0.85f,0.92f,1.0f,1.0f);
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f,0.0f);glVertex2f(label_left,label_bottom+label_h);
        glTexCoord2f(1.0f,0.0f);glVertex2f(label_left+label_w,label_bottom+label_h);
        glTexCoord2f(1.0f,1.0f);glVertex2f(label_left+label_w,label_bottom);
        glTexCoord2f(0.0f,1.0f);glVertex2f(label_left,label_bottom);
        glEnd();
        glDisable(GL_TEXTURE_2D);
    }
    glBegin(GL_QUADS);
    /* (twelve dots; the head of the turn is the brightest, the tail fades) */
    for(dot=0;dot<12;++dot) {
        float turn=loading.wobble*40.0f; /* (the wobble advances 0.01 a frame) */
        int head=(int)turn%12;
        int age=(head-dot+12)%12;
        float angle=(float)dot*(float)(2.0*LOADING_PI/12.0);
        float x=centre_x+(float)sin(angle)*radius,y=centre_y+(float)cos(angle)*radius;
        glColor4f(0.85f,0.92f,1.0f,1.0f-(float)age*0.08f);
        glVertex2f(x-dot_size,y-dot_size);glVertex2f(x+dot_size,y-dot_size);
        glVertex2f(x+dot_size,y+dot_size);glVertex2f(x-dot_size,y+dot_size);
    }
    glColor4f(0.85f,0.92f,1.0f,0.25f);
    glVertex2f(bar_left,bar_y);glVertex2f(bar_right,bar_y);glVertex2f(bar_right,bar_y+bar_h);glVertex2f(bar_left,bar_y+bar_h);
    glColor4f(0.85f,0.92f,1.0f,0.9f);
    {
        float fill=bar_left+(bar_right-bar_left)*(t<0.0f?0.0f:t>1.0f?1.0f:t);
        glVertex2f(bar_left,bar_y);glVertex2f(fill,bar_y);glVertex2f(fill,bar_y+bar_h);glVertex2f(bar_left,bar_y+bar_h);
    }
    glEnd();
    glDisable(GL_BLEND);
}

static void write_shot(unsigned width,unsigned height,const char *path)
{
    unsigned char *pixels;
    unsigned y;
    FILE *file;
    pixels=(unsigned char *)malloc((size_t)width*height*3);
    if(!pixels)return;
    glPixelStorei(GL_PACK_ALIGNMENT,1);
    glReadPixels(0,0,(GLsizei)width,(GLsizei)height,GL_RGB,GL_UNSIGNED_BYTE,pixels);
    file=fopen(path,"wb");
    if(file) {
        fprintf(file,"P6\n%u %u\n255\n",width,height);
        for(y=height;y>0;--y)fwrite(pixels+(size_t)(y-1)*width*3,1,(size_t)width*3,file);
        fclose(file);
    }
    free(pixels);
}

void mac_loading_draw(unsigned width,unsigned height,float progress,const char *shot_path)
{
    GLhandleARB old_program;
    float w,h,unit,t,tone,alpha,position,blur=0.1f;
    int pass,window_width=0,window_height=0;
    static const float taps[4][2]={{-1.0f,1.0f},{-1.0f,-1.0f},{1.0f,-1.0f},{1.0f,1.0f}};
    if(!loading.started||!width||!height)return;
    /* port: over the whole window, as the movies are: the game's picture is 640x480 (or wider), but the
       window's framebuffer is not, and a viewport of the picture's size sits in its lower left corner */
    platform_video_window_size(&window_width,&window_height);
    if(window_width>0&&window_height>0){width=(unsigned)window_width;height=(unsigned)window_height;}
    w=(float)width;h=(float)height;
    unit=h/480.0f; /* (the Xbox's band is 128 pixels wide in its 480 lines) */
    old_program=glGetHandleARB(GL_PROGRAM_OBJECT_ARB);
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
    glMatrixMode(GL_PROJECTION);glPushMatrix();glLoadIdentity();
    glOrtho(0.0,(double)width,0.0,(double)height,-1.0,1.0);
    glMatrixMode(GL_MODELVIEW);glPushMatrix();glLoadIdentity();
    glUseProgramObjectARB(0);
    glDisable(GL_VERTEX_PROGRAM_ARB);glDisable(GL_FRAGMENT_PROGRAM_ARB);
    glDisable(GL_DEPTH_TEST);glDisable(GL_STENCIL_TEST);glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glDisable(GL_ALPHA_TEST);
    glDisable(GL_LIGHTING);glDisable(GL_FOG);glDisable(GL_COLOR_LOGIC_OP);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glViewport(0,0,(GLsizei)width,(GLsizei)height);
    glActiveTexture(GL_TEXTURE0);
    glDisable(GL_TEXTURE_2D);glDisable(GL_TEXTURE_RECTANGLE_ARB);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
    if(!loading.mask)make_mask();
    if(loading.feedback_ready&&(loading.feedback_width!=width||loading.feedback_height!=height)) {
        /* (the window changed size: the fade starts again from black) */
        glDeleteTextures(1,&loading.feedback);loading.feedback=0;loading.feedback_ready=0;
    }
    if(!loading.feedback_ready)make_feedback(width,height);

    /* progress_bar_render: the progress follows the one given, with a wobble */
    loading.wobble+=0.01f;
    t=(float)((sin(loading.wobble)*0.05+progress-loading.last)*0.2+loading.last);
    if(t>1.0f)t=1.0f;
    loading.last=t;
    tone=(float)pow(sin(t*LOADING_PI),0.9);
    if(tone<0.0f)tone=0.0f;
    alpha=t<0.9f?0.9f:0.9f-(t-0.9f)*9.0f;
    if(alpha<0.0f)alpha=0.0f;

    glClearColor(0.0f,0.0f,0.0f,1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    /* the fade layer: the last frame, four taps a tenth of a texel apart, 254/255 and 254/255 and 1 of it */
    glEnable(GL_TEXTURE_RECTANGLE_ARB);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,loading.feedback);
    glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE);
    glColor4f(alpha*0.25f*254.0f/255.0f,alpha*0.25f*254.0f/255.0f,alpha*0.25f,1.0f);
    for(pass=0;pass<4;++pass) {
        float dx=taps[pass][0]*blur,dy=taps[pass][1]*blur;
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f+dx,0.0f+dy);glVertex2f(0.0f,0.0f);
        glTexCoord2f(w+dx,0.0f+dy);glVertex2f(w,0.0f);
        glTexCoord2f(w+dx,h+dy);glVertex2f(w,h);
        glTexCoord2f(0.0f+dx,h+dy);glVertex2f(0.0f,h);
        glEnd();
    }
    glDisable(GL_TEXTURE_RECTANGLE_ARB);
    /* the band: the mask, 128 pixels wide and the screen high, moving across as the load does */
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D,loading.mask);
    glColor4f(0.55f*BAND_LEVEL*tone,0.75f*BAND_LEVEL*tone,BAND_LEVEL*tone,1.0f);
    position=(t*768.0f-64.0f-64.0f)*unit;
    glBegin(GL_QUADS);
    glTexCoord2f((0.0f-position)/(MASK_WIDTH*unit),0.0f);glVertex2f(0.0f,h);
    glTexCoord2f((w-position)/(MASK_WIDTH*unit),0.0f);glVertex2f(w,h);
    glTexCoord2f((w-position)/(MASK_WIDTH*unit),1.0f);glVertex2f(w,0.0f);
    glTexCoord2f((0.0f-position)/(MASK_WIDTH*unit),1.0f);glVertex2f(0.0f,0.0f);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    /* what the next frame fades */
    glEnable(GL_TEXTURE_RECTANGLE_ARB);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,loading.feedback);
    glCopyTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB,0,0,0,0,0,(GLsizei)width,(GLsizei)height);
    glDisable(GL_TEXTURE_RECTANGLE_ARB);
    if(config_boolean("mac.loading_indicator"))draw_indicator(w,h,unit,t);
    if(shot_path&&*shot_path)write_shot(width,height,shot_path);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);
    glBindTexture(GL_TEXTURE_2D,0);
    glMatrixMode(GL_MODELVIEW);glPopMatrix();
    glMatrixMode(GL_PROJECTION);glPopMatrix();
    glPopClientAttrib();
    glPopAttrib();
    glUseProgramObjectARB(old_program);
    while(glGetError()!=GL_NO_ERROR){}
}

float mac_loading_progress(void)
{
    return loading.last;
}

void mac_loading_stop(void)
{
    if(loading.feedback)glDeleteTextures(1,&loading.feedback);
    if(loading.mask)glDeleteTextures(1,&loading.mask);
    if(loading.label)glDeleteTextures(1,&loading.label);
    free(loading.base);
    memset(&loading,0,sizeof(loading));
}
#else
void mac_loading_start(const unsigned *pixels,unsigned width,unsigned height,unsigned pitch_pixels)
{(void)pixels;(void)width;(void)height;(void)pitch_pixels;}
void mac_loading_draw(unsigned width,unsigned height,float progress,const char *shot_path)
{(void)width;(void)height;(void)progress;(void)shot_path;}
float mac_loading_progress(void){return 0.0f;}
void mac_loading_stop(void){}
#endif
