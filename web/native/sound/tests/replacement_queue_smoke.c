/* Exercises the patched REAL sound_mods.c queue and decoder. Static inclusion
 * permits bounded failure injection without adding production test exports. */
#include "sound_mods.c"
#include <assert.h>
#include <emscripten.h>

static void put32(unsigned char *p, unsigned n) { for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(n>>(i*8)); }
static void oversized_wav(const char *path, unsigned bytes)
{
    unsigned char header[44]={0}, block[4096];
    memcpy(header,"RIFF",4);put32(header+4,36+bytes);memcpy(header+8,"WAVEfmt ",8);
    put32(header+16,16);header[20]=1;header[22]=1;put32(header+24,32728);put32(header+28,32728);
    header[32]=1;header[34]=8;memcpy(header+36,"data",4);put32(header+40,bytes);
    memset(block,128,sizeof(block));
    FILE *file=fopen(path,"wb");assert(file);assert(fwrite(header,1,sizeof(header),file)==sizeof(header));
    for(unsigned done=0;done<bytes;) { unsigned count=bytes-done<sizeof(block)?bytes-done:sizeof(block); assert(fwrite(block,1,count,file)==count);done+=count; }
    assert(fclose(file)==0);
}
int main(void)
{
    KhdaysReplacement decoded;
    char why[160];
    const int16_t samples[4]={-100,100,-200,200};
    Entry first={0}, missing={0}, capped[65]={0};
    assert(khdays_replacement_write_wav("valid.wav",32728,1,samples,4,0,0,0));
    oversized_wav("large-pcm.wav",4u*1024u*1024u+1u);
    assert(!khdays_replacement_load(&decoded,"large-pcm.wav",NULL,why,sizeof(why)));
    assert(strstr(why,"bounded replacement decode budget")!=NULL && decoded.samples==NULL);
    oversized_wav("large-file.wav",8u*1024u*1024u+1u);
    assert(!khdays_replacement_load(&decoded,"large-file.wav",NULL,why,sizeof(why)));
    assert(strcmp(why,"cannot be read")==0 && decoded.samples==NULL);
    snprintf(first.path,sizeof(first.path),"valid.wav");first.state=LET_GO;first.next=&missing;entries=&first;
    snprintf(missing.path,sizeof(missing.path),"missing.wav");missing.state=LET_GO;
    enqueue(&first,0);enqueue(&missing,0);enqueue(&missing,1);
    assert(queued==2 && queue[0]==&missing && queue[1]==&first && decoder_pending);
    browser_cancel_decoder();
    khdays_sound_mods_browser_decode_one();
    assert(missing.state==FAILED && first.state==QUEUED && queued==1);
    khdays_sound_mods_browser_stop();
    assert(first.state==LET_GO && queued==0 && !decoder_pending);
    enqueue(&first,1);emscripten_sleep(12);
    assert(first.state==READY && first.file.frames==4 && first.file.samples[0]==-100 && queued==0);
    for(int i=0;i<65;i++){capped[i].state=LET_GO;enqueue(&capped[i],0);}
    assert(queued==64 && capped[64].state==LET_GO && queue_room==64);
    khdays_sound_mods_browser_stop();emscripten_sleep(4);
    assert(queued==0 && !decoder_pending);
    for(int i=0;i<65;i++)assert(capped[i].state==LET_GO);
    khdays_replacement_free(&first.file);entries=NULL;
    remove("valid.wav");remove("large-pcm.wav");remove("large-file.wav");
    puts("Browser replacement decode: encoded/decoded limits, real WAV decode, priority/dedupe, one-file turns, bounded queue and cancellation passed");
    return 0;
}
