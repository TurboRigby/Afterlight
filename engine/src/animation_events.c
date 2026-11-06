/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/animation.h>
#include <math.h>
#include <string.h>
bool af_animation_collect_events(const af_animation_clip *c,double time,double delta,bool loop,af_animation_events *out){
    if(!c||!out||!isfinite(time)||fabs(time)>1e9||!isfinite(delta)||fabs(delta)>8640000||!isfinite(c->duration)||c->duration<=0||c->event_count>AF_ANIMATION_MAX_EVENTS)return false;
    *out=(af_animation_events){0};if(delta==0)return true;double end=time+delta,d=c->duration,next[AF_ANIMATION_MAX_EVENTS];uint64_t remaining[AF_ANIMATION_MAX_EVENTS],total=0;
    for(size_t i=0;i<c->event_count;++i){double t=c->events[i].time;if(!isfinite(t)||t<0||t>=d||!c->events[i].name[0]||!memchr(c->events[i].name,0,sizeof(c->events[i].name)))return false;
        uint64_t n=0;if(loop){double a,b;if(delta>0){a=floor((time-t)/d)+1;b=floor((end-t)/d);next[i]=t+a*d;}else{a=ceil((time-t)/d)-1;b=ceil((end-t)/d);next[i]=t+a*d;}double count=delta>0?b-a+1:a-b+1;if(count>1e12)return false;if(count>0)n=(uint64_t)count;}
        else{next[i]=t;n=(delta>0?time<t&&t<=end:end<=t&&t<time)?1u:0u;}remaining[i]=n;total+=n;
    }
    while(out->count<AF_ANIMATION_EVENT_BATCH){size_t best=c->event_count;for(size_t i=0;i<c->event_count;++i)if(remaining[i]&&(best==c->event_count||(delta>0?next[i]<next[best]:next[i]>next[best])))best=i;if(best==c->event_count)break;
        out->items[out->count++]=c->events[best];--remaining[best];next[best]+=delta>0?d:-d;}
    out->dropped=total-out->count;return true;
}
