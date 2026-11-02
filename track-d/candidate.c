// Speed Demon 2026 Track D: independent experimental C11 implementation.
// Build: cc -O3 -march=native -flto -pthread -std=c11 -Wall -Wextra -o candidate candidate.c
// Never promote without official checksum, sandbox, and score evidence.
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <pthread.h>
#include <stdatomic.h>
#include <limits.h>
#include <errno.h>
#define INF UINT64_MAX
#define NB 65
#define W_BITS 11
#define W_SENTINEL ((1u << W_BITS) - 1)
typedef struct { const unsigned char *p, *end; size_t bytes; } Text;
static void die(const char *s) { perror(s); exit(1); }
static void *xmalloc(size_t n) { void *p = malloc(n?n:1); if(!p) die("malloc"); return p; }
static void *xcalloc(size_t n, size_t s) { void *p=calloc(n?n:1,s); if(!p)die("calloc");return p; }
static void *xrealloc(void *p,size_t s){void *q=realloc(p,s?s:1);if(!q)die("realloc");return q;}
static Text open_text(const char *path) {
    int fd=open(path,O_RDONLY);if(fd<0)die(path);
    struct stat st;if(fstat(fd,&st)<0)die("fstat");
    Text t={0};t.bytes=(size_t)st.st_size;
    if(t.bytes){t.p=mmap(NULL,t.bytes,PROT_READ,MAP_PRIVATE,fd,0);if(t.p==MAP_FAILED)die("mmap");t.end=t.p+t.bytes;}
    close(fd);return t;
}
static inline uint32_t parse_u32(const unsigned char **pp,const unsigned char *end){
    const unsigned char *p=*pp;uint32_t v=0;
    while(p<end && *p>='0' && *p<='9'){v=v*10+(*p-'0');++p;}
    if(p<end)++p;
    *pp=p;
    return v;
}
typedef struct {uint32_t u,v,w;} Query;
typedef struct {uint32_t index,weight;} Heavy;
typedef struct {Heavy *a;size_t len,cap;} HeavyVec;
static inline void heavy_push(HeavyVec *h,uint32_t i,uint32_t w){
    if(h->len==h->cap){h->cap=h->cap?2*h->cap:128;h->a=xrealloc(h->a,h->cap*sizeof(Heavy));}
    h->a[h->len++]=(Heavy){i,w};
}
static int cmp_heavy(const void *a,const void*b){uint32_t x=((const Heavy*)a)->index,y=((const Heavy*)b)->index;return (x>y)-(x<y);}
typedef struct {uint32_t *off,*adj;Heavy *heavy;size_t nh;} Side;
typedef struct {Side f,r;uint32_t n,m,mask,shift;} Graph;
static inline uint32_t edge_w(const Side *g,uint32_t edge,uint32_t word,uint32_t shift){
    uint32_t w=word>>shift;
    if(w!=W_SENTINEL)return w;
    size_t l=0,r=g->nh;
    while(l<r){size_t m=l+(r-l)/2;if(g->heavy[m].index<edge)l=m+1;else r=m;}
    if(l>=g->nh || g->heavy[l].index!=edge){fprintf(stderr,"missing heavy edge %u\n",edge);exit(3);}
    return g->heavy[l].weight;
}
static Graph make_graph(Text text){
    const unsigned char *p=text.p;uint32_t n=parse_u32(&p,text.end),m=parse_u32(&p,text.end);
    if(!n||n>2097152u){fprintf(stderr,"unsupported node count %u\n",n);exit(2);}
    uint32_t *outdeg=xcalloc(n+1,sizeof(uint32_t)),*indeg=xcalloc(n+1,sizeof(uint32_t));
    const unsigned char *start=p;
    for(uint32_t i=0;i<m;++i){
        uint32_t u=parse_u32(&p,text.end),v=parse_u32(&p,text.end);
        (void)parse_u32(&p,text.end);
        if(u>=n||v>=n){fprintf(stderr,"node out of range\n");exit(2);}
        ++outdeg[u+1];++indeg[v+1];
    }
    for(uint32_t u=0;u<n;++u){outdeg[u+1]+=outdeg[u];indeg[u+1]+=indeg[u];}
    Graph g={0};g.n=n;g.m=m;g.shift=n>1u ? 32u-(uint32_t)__builtin_clz(n-1u) : 1u;
    g.mask=(1u<<g.shift)-1u;
    if(g.shift+W_BITS>32){fprintf(stderr,"packed edge cannot represent %u nodes\n",n);exit(2);}
    g.f.off=outdeg;g.r.off=indeg;
    g.f.adj=xmalloc((size_t)m*sizeof(uint32_t));g.r.adj=xmalloc((size_t)m*sizeof(uint32_t));
    uint32_t *fc=xmalloc((size_t)n*sizeof(uint32_t)),*rc=xmalloc((size_t)n*sizeof(uint32_t));
    memcpy(fc,outdeg,(size_t)n*sizeof(uint32_t));memcpy(rc,indeg,(size_t)n*sizeof(uint32_t));
    HeavyVec fh={0},rh={0};p=start;
    for(uint32_t i=0;i<m;++i){
        uint32_t u=parse_u32(&p,text.end),v=parse_u32(&p,text.end),w=parse_u32(&p,text.end);
        uint32_t fi=fc[u]++,ri=rc[v]++;
        uint32_t packed=(w<W_SENTINEL?w:W_SENTINEL);
        g.f.adj[fi]=v | (packed<<g.shift);
        g.r.adj[ri]=u | (packed<<g.shift);
        if(w>=W_SENTINEL){heavy_push(&fh,fi,w);heavy_push(&rh,ri,w);}
    }
    free(fc);free(rc);
    qsort(fh.a,fh.len,sizeof(Heavy),cmp_heavy);qsort(rh.a,rh.len,sizeof(Heavy),cmp_heavy);
    g.f.heavy=fh.a;g.f.nh=fh.len;g.r.heavy=rh.a;g.r.nh=rh.len;
    if(text.bytes)munmap((void*)text.p,text.bytes);
    return g;
}
typedef struct {uint64_t key;uint32_t vertex;} Item;
typedef struct {Item *a;uint32_t len,cap;} Bucket;
typedef struct {Bucket b[NB];uint64_t last;uint32_t len;} Heap;
static inline unsigned bucket_of(uint64_t k,uint64_t last){uint64_t x=k^last;return x?64u-(unsigned)__builtin_clzll(x):0;}
static inline void bp(Bucket *b,Item e){if(b->len==b->cap){b->cap=b->cap?b->cap*2:16;b->a=xrealloc(b->a,(size_t)b->cap*sizeof(Item));}b->a[b->len++]=e;}
static inline void hp(Heap*h,uint64_t k,uint32_t v){bp(&h->b[bucket_of(k,h->last)],(Item){k,v});++h->len;}
static inline uint64_t top(Heap*h){
    if(!h->b[0].len){
        unsigned i=1;while(i<NB&&!h->b[i].len)++i;
        if(i==NB){fprintf(stderr,"empty radix heap\n");exit(4);}
        Bucket *b=&h->b[i];uint64_t minimum=INF;
        for(uint32_t j=0;j<b->len;++j)if(b->a[j].key<minimum)minimum=b->a[j].key;
        h->last=minimum;
        for(uint32_t j=0;j<b->len;++j){Item x=b->a[j];bp(&h->b[bucket_of(x.key,minimum)],x);}
        b->len=0;
    }
    return h->last;
}
static inline Item pop(Heap*h){(void)top(h);--h->len;return h->b[0].a[--h->b[0].len];}
static void reset_heap(Heap*h){for(unsigned i=0;i<NB;++i)h->b[i].len=0;h->last=0;h->len=0;}
typedef struct{uint32_t node,epoch;uint64_t distance;} Entry;
typedef struct{Entry *a;uint32_t cap,used,epoch;} Map;
static inline uint32_t mix(uint32_t x){x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;return x^(x>>16);}
static void rehash(Map *m,uint32_t newcap){
    Entry*old=m->a;uint32_t oldcap=m->cap;
    m->a=xcalloc(newcap,sizeof(Entry));m->cap=newcap;m->used=0;
    for(uint32_t i=0;i<oldcap;++i)if(old[i].epoch==m->epoch){
        Entry e=old[i];uint32_t j=mix(e.node)&(newcap-1u);
        while(m->a[j].epoch==m->epoch)j=(j+1u)&(newcap-1u);
        m->a[j]=e;++m->used;
    }
    free(old);
}
static inline uint64_t map_get(const Map*m,uint32_t v){
    if(!m->cap)return INF;
    uint32_t j=mix(v)&(m->cap-1u);
    while(m->a[j].epoch==m->epoch){if(m->a[j].node==v)return m->a[j].distance;j=(j+1u)&(m->cap-1u);}
    return INF;
}
static inline int map_improve(Map*m,uint32_t v,uint64_t d){
    if(!m->cap){m->epoch=1;rehash(m,4096);}
    if((uint64_t)(m->used+1)*10>= (uint64_t)m->cap*7)rehash(m,m->cap*2);
    uint32_t j=mix(v)&(m->cap-1u);
    while(m->a[j].epoch==m->epoch){
        if(m->a[j].node==v){if(d<m->a[j].distance){m->a[j].distance=d;return 1;}return 0;}
        j=(j+1u)&(m->cap-1u);
    }
    m->a[j]=(Entry){v,m->epoch,d};++m->used;return 1;
}
static inline void reset_map(Map*m){
    m->used=0;
    if(++m->epoch==0){memset(m->a,0,(size_t)m->cap*sizeof(Entry));m->epoch=1;}
}
typedef struct {const Graph *g;const Query*q;uint32_t nq;uint64_t *answers;atomic_uint *next;} Work;
static uint64_t solve(const Graph*g,uint32_t s,uint32_t t,Map dist[2],Heap q[2]){
    if(s==t)return 0;
    if(g->r.off[t]==g->r.off[t+1]||g->f.off[s]==g->f.off[s+1])return INF;
    map_improve(&dist[0],s,0);hp(&q[0],0,s);
    map_improve(&dist[1],t,0);hp(&q[1],0,t);
    uint64_t best=INF;
    const Side *side[2]={&g->f,&g->r};
    while(q[0].len&&q[1].len){
        uint64_t a=top(&q[0]),b=top(&q[1]);
        if(best!=INF && a+b>=best)break;
        unsigned d=(q[1].len<q[0].len);
        Item curr=pop(&q[d]);
        if(curr.key!=map_get(&dist[d],curr.vertex))continue;
        const Side *adj=side[d];
        for(uint32_t i=adj->off[curr.vertex];i<adj->off[curr.vertex+1];++i){
            uint32_t word=adj->adj[i],v=word & g->mask;
            uint64_t nd=curr.key+(uint64_t)edge_w(adj,i,word,g->shift);
            if(nd<best && map_improve(&dist[d],v,nd)){
                hp(&q[d],nd,v);
                uint64_t back=map_get(&dist[!d],v);
                if(back!=INF&&nd+back<best)best=nd+back;
            }
        }
    }
    return best;
}
static void *worker(void*arg){
    Work *w=arg;Map dist[2]={0};Heap q[2]={0};
    for(;;){
        uint32_t i=atomic_fetch_add_explicit(w->next,1,memory_order_relaxed);
        if(i>=w->nq)break;
        w->answers[i]=solve(w->g,w->q[i].u,w->q[i].v,dist,q);
        reset_heap(&q[0]);reset_heap(&q[1]);
        reset_map(&dist[0]);reset_map(&dist[1]);
    }
    free(dist[0].a);free(dist[1].a);
    for(unsigned d=0;d<2;++d)for(unsigned b=0;b<NB;++b)free(q[d].b[b].a);
    return NULL;
}
static Query *read_queries(Text t,uint32_t *count){
    uint32_t n=0,cap=256;Query *q=xmalloc((size_t)cap*sizeof(Query));
    const unsigned char *p=t.p;
    while(p<t.end){uint32_t s=parse_u32(&p,t.end),v=parse_u32(&p,t.end);
        if(n==cap){cap*=2;q=xrealloc(q,(size_t)cap*sizeof(Query));}
        q[n++]=(Query){s,v,0};
    }
    if(t.bytes)munmap((void*)t.p,t.bytes);
    *count=n;return q;
}
int main(int argc,char **argv){
    const char *in=argc>1?argv[1]:"/data",*out=argc>2?argv[2]:"/out/distances.txt";
    char f[4096];
    if(snprintf(f,sizeof(f),"%s/queries.txt",in)>=(int)sizeof(f))exit(2);
    uint32_t nq;Query *queries=read_queries(open_text(f),&nq);
    if(snprintf(f,sizeof(f),"%s/graph.txt",in)>=(int)sizeof(f))exit(2);
    Graph g=make_graph(open_text(f));
    for(uint32_t i=0;i<nq;++i)if(queries[i].u>=g.n||queries[i].v>=g.n)exit(2);
    uint64_t *ans=xmalloc((size_t)nq*sizeof(uint64_t));
    unsigned nt=(unsigned)sysconf(_SC_NPROCESSORS_ONLN);if(nt>12)nt=12;if(!nt)nt=1;
    if(nq<nt)nt=nq?nq:1;
    pthread_t *threads=xmalloc((size_t)nt*sizeof(pthread_t));
    atomic_uint next=0;Work w={&g,queries,nq,ans,&next};
    for(unsigned t=0;t<nt;++t)if(pthread_create(&threads[t],NULL,worker,&w))die("pthread_create");
    for(unsigned t=0;t<nt;++t)pthread_join(threads[t],NULL);
    FILE *fp=fopen(out,"wb");if(!fp)die("fopen output");
    for(uint32_t i=0;i<nq;++i){
        if(ans[i]==INF)fputs("-1\n",fp);else fprintf(fp,"%llu\n",(unsigned long long)ans[i]);
    }
    if(fclose(fp))die("fclose");
    free(threads);free(ans);free(queries);
    free(g.f.off);free(g.r.off);free(g.f.adj);free(g.r.adj);free(g.f.heavy);free(g.r.heavy);
    return 0;
}