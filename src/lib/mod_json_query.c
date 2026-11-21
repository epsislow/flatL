
#include "flat_native_mod.h"
#include "flat_native_util.h"
#include "flat_number.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JQ_MAX_TOKENS 512
#define JQ_MAX_NODES 256
#define JQ_MAX_SEGS 32
#define JQ_MAX_VALUES 1024
#define JQ_TEXT_MAX 256

typedef enum { JT_EOF, JT_DOLLAR, JT_DOT, JT_LB, JT_RB, JT_LP, JT_RP, JT_COMMA,
               JT_STAR, JT_AT, JT_OP, JT_STR, JT_NUM, JT_ID } JqTokKind;
typedef struct { JqTokKind k; char s[JQ_TEXT_MAX]; double n; int pos; } JqTok;
typedef struct { JqTok t[JQ_MAX_TOKENS]; int n, i; const char *src; } JqLex;
typedef enum { JS_KEY, JS_INDEX, JS_STAR, JS_LISTMARK, JS_AT } JqSegKind;
typedef struct { JqSegKind k; char key[JQ_TEXT_MAX]; int ix; } JqSeg;
typedef struct { JqSeg s[JQ_MAX_SEGS]; int n; } JqPath;
typedef enum { JN_AND, JN_OR, JN_NOT, JN_EXISTS, JN_CMP, JN_BETWEEN, JN_IS,
               JN_LEN, JN_IN, JN_HAS, JN_CONTAINS, JN_STARTS, JN_ENDS, JN_TYPE } JqNodeKind;
typedef struct {
    JqNodeKind k; int a,b; JqPath path; char op[3]; char word[16];
    double x,y; FlatValue vals[32]; int val_n; int flag; int negate;
} JqNode;
typedef struct { JqNode n[JQ_MAX_NODES]; int count, root; } JqAst;
typedef struct { FlatValue v[JQ_MAX_VALUES]; int n; int missing; } JqValues;
typedef struct { int idx[JQ_MAX_SEGS]; int n; } JqContext;

static void jq_copy_text(char *dst,size_t cap,const char *src) {
    size_t n=strlen(src); if(n>=cap)n=cap-1; memcpy(dst,src,n); dst[n]='\0';
}
static int jq_addtok(JqLex *l, JqTokKind k, const char *s, size_t n, int pos) {
    JqTok *t;
    if (l->n >= JQ_MAX_TOKENS || n >= JQ_TEXT_MAX) return -1;
    t=&l->t[l->n++]; memset(t,0,sizeof(*t)); t->k=k; t->pos=pos;
    if (s && n) { memcpy(t->s,s,n); t->s[n]='\0'; }
    return 0;
}
static int jq_lex(const char *s, JqLex *l) {
    int i=0; memset(l,0,sizeof(*l)); l->src=s;
    while (s[i]) {
        int st=i; char c=s[i];
        if (isspace((unsigned char)c)) { i++; continue; }
        if (c=='$') { if(jq_addtok(l,JT_DOLLAR,NULL,0,i++)) return -1; continue; }
        if (c=='.') { if(jq_addtok(l,JT_DOT,NULL,0,i++)) return -1; continue; }
        if (c=='[') { if(jq_addtok(l,JT_LB,NULL,0,i++)) return -1; continue; }
        if (c==']') { if(jq_addtok(l,JT_RB,NULL,0,i++)) return -1; continue; }
        if (c=='(') { if(jq_addtok(l,JT_LP,NULL,0,i++)) return -1; continue; }
        if (c==')') { if(jq_addtok(l,JT_RP,NULL,0,i++)) return -1; continue; }
        if (c==',') { if(jq_addtok(l,JT_COMMA,NULL,0,i++)) return -1; continue; }
        if (c=='*') { if(jq_addtok(l,JT_STAR,NULL,0,i++)) return -1; continue; }
        if (c=='@') { if(jq_addtok(l,JT_AT,NULL,0,i++)) return -1; continue; }
        if (c=='='||c=='!'||c=='<'||c=='>') {
            i++; if ((c=='!'||c=='<'||c=='>') && s[i]=='=') i++;
            if(c=='!' && s[i-1]!='=') return -1;
            if(jq_addtok(l,JT_OP,s+st,(size_t)(i-st),st)) return -1;
            continue;
        }
        if(c=='"') {
            char b[JQ_TEXT_MAX]; int z=0; i++;
            while(s[i] && s[i]!='"') {
                char v=s[i++];
                if(v=='\\') { v=s[i++]; if(!v) return -1;
                    if(v=='n') v='\n'; else if(v=='r') v='\r'; else if(v=='t') v='\t';
                }
                if(z+1>=JQ_TEXT_MAX) return -1;
                b[z++]=v;
            }
            if(s[i]!='"') return -1;
            i++;
            if(jq_addtok(l,JT_STR,b,(size_t)z,st)) return -1;
            continue;
        }
        if(c=='-' || isdigit((unsigned char)c)) {
            char *e; double v=strtod(s+i,&e);
            if(e==s+i || !isfinite(v)) return -1;
            i=(int)(e-s); if(jq_addtok(l,JT_NUM,s+st,(size_t)(i-st),st)) return -1;
            l->t[l->n-1].n=v; continue;
        }
        if(isalpha((unsigned char)c)||c=='_') {
            i++; while(isalnum((unsigned char)s[i])||s[i]=='_') i++;
            if(jq_addtok(l,JT_ID,s+st,(size_t)(i-st),st)) return -1;
            continue;
        }
        return -1;
    }
    return jq_addtok(l,JT_EOF,NULL,0,i);
}
static JqTok *jq_cur(JqLex *l) { return &l->t[l->i]; }
static int jq_is(JqLex *l,JqTokKind k) { return jq_cur(l)->k==k; }
static int jq_kw(JqLex *l,const char *w) { return jq_is(l,JT_ID)&&strcasecmp(jq_cur(l)->s,w)==0; }
static int jq_takekw(JqLex *l,const char *w) { if(!jq_kw(l,w)) return 0; l->i++; return 1; }
static int jq_new(JqAst *a,JqNodeKind k) {
    int id=a->count++; if(id>=JQ_MAX_NODES) { a->count--; return -1; }
    memset(&a->n[id],0,sizeof(a->n[id])); a->n[id].k=k; a->n[id].a=a->n[id].b=-1; return id;
}
static int jq_path(JqLex *l,JqPath *p,int allow_at) {
    if(!jq_is(l,JT_DOLLAR)) return -1;
    l->i++; memset(p,0,sizeof(*p));
    while(p->n<JQ_MAX_SEGS) {
        JqSeg *g;
        if(jq_is(l,JT_DOT)) {
            l->i++; if(!jq_is(l,JT_ID)&&!jq_is(l,JT_STR)) return -1;
            g=&p->s[p->n++]; memset(g,0,sizeof(*g)); g->k=JS_KEY;
            snprintf(g->key,sizeof(g->key),"%s",jq_cur(l)->s); l->i++; continue;
        }
        if(jq_is(l,JT_LB)) {
            l->i++;
            if(jq_is(l,JT_RB)) { g=&p->s[p->n++]; memset(g,0,sizeof(*g)); g->k=JS_LISTMARK; l->i++; continue; }
            if(jq_is(l,JT_STAR)|| (allow_at&&jq_is(l,JT_AT))) {
                g=&p->s[p->n++]; memset(g,0,sizeof(*g)); g->k=jq_is(l,JT_STAR)?JS_STAR:JS_AT; l->i++;
                if(!jq_is(l,JT_RB)) return -1;
                l->i++; continue;
            }
            if(jq_is(l,JT_NUM)) {
                double d=jq_cur(l)->n; if(d<0||floor(d)!=d||d>1000000) return -1;
                g=&p->s[p->n++]; memset(g,0,sizeof(*g)); g->k=JS_INDEX; g->ix=(int)d; l->i++;
                if(!jq_is(l,JT_RB)) return -1;
                l->i++; continue;
            }
            return -1;
        }
        break;
    }
    return 0;
}
static int jq_value(JqLex *l,FlatValue *v) {
    memset(v,0,sizeof(*v));
    if(jq_is(l,JT_STR)) { flat_value_set_str_inline(v,jq_cur(l)->s,strlen(jq_cur(l)->s)); l->i++; return 0; }
    if(jq_is(l,JT_NUM)) { flat_value_set_number(v,jq_cur(l)->n); l->i++; return 0; }
    if(jq_takekw(l,"true")) return flat_value_set_bool(v,1);
    if(jq_takekw(l,"false")) return flat_value_set_bool(v,0);
    if(jq_takekw(l,"null")) return flat_value_set_null(v);
    return -1;
}
static int jq_expr(JqLex*,JqAst*);
static int jq_pred(JqLex *l,JqAst *a) {
    int id,negated=0; JqNode *n;
    if(jq_is(l,JT_LP)) { l->i++; id=jq_expr(l,a); if(id<0||!jq_is(l,JT_RP)) return -1; l->i++; return id; }
    id=jq_new(a,JN_EXISTS); if(id<0) return -1; n=&a->n[id];
    if(jq_path(l,&n->path,0)) return -1;
    if(jq_is(l,JT_EOF)||jq_is(l,JT_RP)||jq_kw(l,"and")||jq_kw(l,"or")) return id;
    if(jq_takekw(l,"not")) {
        if(!(jq_kw(l,"in")||jq_kw(l,"has")||jq_kw(l,"contains")||jq_kw(l,"starts")||jq_kw(l,"ends")||jq_kw(l,"type"))) return -1;
        negated=1;
    }
    if(jq_takekw(l,"between")) { if(negated) return -1;
        n->k=JN_BETWEEN;
        if(!jq_is(l,JT_NUM)) return -1;
        n->x=jq_cur(l)->n; l->i++;
        if(!jq_takekw(l,"to")||!jq_is(l,JT_NUM)) return -1;
        n->y=jq_cur(l)->n; l->i++; return id;
    }
    if(jq_is(l,JT_OP)) {
        if(negated) return -1;
        n->k=JN_CMP; jq_copy_text(n->op,sizeof(n->op),jq_cur(l)->s); l->i++;
        if(jq_value(l,&n->vals[0])) return -1;
        n->val_n=1; return id;
    }
    if(jq_takekw(l,"is")) {
        if(negated) return -1;
        n->k=JN_IS;
        if(jq_takekw(l,"not")) { if(!jq_takekw(l,"null")) return -1; n->flag=1; strcpy(n->word,"null"); }
        else if(jq_takekw(l,"null")) strcpy(n->word,"null");
        else if(jq_takekw(l,"true")) strcpy(n->word,"true");
        else if(jq_takekw(l,"false")) strcpy(n->word,"false"); else return -1;
        return id;
    }
    if(jq_takekw(l,"len")) {
        if(negated) return -1;
        n->k=JN_LEN;
        if(jq_is(l,JT_OP)) { jq_copy_text(n->op,sizeof(n->op),jq_cur(l)->s); l->i++; } else return -1;
        if(!jq_is(l,JT_NUM)) return -1;
        n->x=jq_cur(l)->n; l->i++;
        if(n->path.n && n->path.s[n->path.n-1].k==JS_LISTMARK) { n->path.n--; n->flag=1; }
        return id;
    }
    if(jq_takekw(l,"in")||jq_takekw(l,"has")) {
        int ishas=0; n->k=JN_IN;
        if(strcasecmp(l->t[l->i-1].s,"has")==0) { n->k=JN_HAS; ishas=1; }
        n->negate=negated;
        if(ishas&&jq_takekw(l,"all")) n->flag=1;
        if(!jq_is(l,JT_LB)) {
            if(!ishas || jq_value(l,&n->vals[0])!=0) return -1;
            n->val_n=1; return id;
        }
        l->i++;
        while(!jq_is(l,JT_RB)) {
            if(n->val_n>=32||jq_value(l,&n->vals[n->val_n++])) return -1;
            if(jq_is(l,JT_COMMA)) l->i++; else if(!jq_is(l,JT_RB)) return -1;
        }
        l->i++; return id;
    }
    if(jq_takekw(l,"contains")||jq_takekw(l,"starts")||jq_takekw(l,"ends")) {
        n->negate=negated;
        const char *w=l->t[l->i-1].s;
        n->k=JN_CONTAINS;
        if(!strcasecmp(w,"starts")) { n->k=JN_STARTS; if(!jq_takekw(l,"with")) return -1; }
        if(!strcasecmp(w,"ends")) { n->k=JN_ENDS; if(!jq_takekw(l,"with")) return -1; }
        if(!jq_is(l,JT_STR)) return -1;
        n->vals[0].tag=VAL_STR_INLINE;
        flat_value_set_str_inline(&n->vals[0],jq_cur(l)->s,strlen(jq_cur(l)->s)); n->val_n=1; l->i++; return id;
    }
    if(jq_takekw(l,"type")) {
        n->negate=negated; n->k=JN_TYPE; if(!jq_is(l,JT_ID)) return -1;
        jq_copy_text(n->word,sizeof(n->word),jq_cur(l)->s); l->i++; return id;
    }
    return -1;
}
static int jq_not(JqLex*l,JqAst*a);
static int jq_and(JqLex*l,JqAst*a) {
    int x=jq_not(l,a); if(x<0)return -1;
    while(jq_takekw(l,"and")) { int y=jq_not(l,a),z=jq_new(a,JN_AND); if(y<0||z<0)return -1; a->n[z].a=x;a->n[z].b=y;x=z; }
    return x;
}
static int jq_or(JqLex*l,JqAst*a) {
    int x=jq_and(l,a); if(x<0)return -1;
    while(jq_takekw(l,"or")) { int y=jq_and(l,a),z=jq_new(a,JN_OR); if(y<0||z<0)return -1; a->n[z].a=x;a->n[z].b=y;x=z; }
    return x;
}
static int jq_not(JqLex*l,JqAst*a) {
    if(jq_takekw(l,"not")) { int x=jq_not(l,a),z=jq_new(a,JN_NOT); if(x<0||z<0)return -1; a->n[z].a=x; return z; }
    return jq_pred(l,a);
}
static int jq_expr(JqLex*l,JqAst*a) { return jq_or(l,a); }
static int jq_parse(const char *s,JqAst*a) {
    JqLex l; int x;
    memset(a,0,sizeof(*a));
    if(jq_lex(s,&l)||l.n<2) return -1;
    x=jq_expr(&l,a); if(x<0||!jq_is(&l,JT_EOF)) return -1; a->root=x; return 0;
}
static int jq_getstr(const FlatValue*v,const FlatStringPool*p,char*b,size_t z) {
    return flat_value_get_cstr(v,p,b,z);
}
static int jq_eq(const FlatValue*a,const FlatValue*b,const FlatStringPool*p) {
    if(a->tag==b->tag) return flat_value_equals(a,b,p);
    if((a->tag==VAL_STR_INLINE||a->tag==VAL_STR_REF)&&(b->tag==VAL_STR_INLINE||b->tag==VAL_STR_REF)) return flat_value_equals(a,b,p);
    return 0;
}
static int jq_path_values(const FlatValue*root,const JqPath*p,const JqContext*ctx,
                          FlatCollectionPool*c,FlatValue*out,int *outn,int collect) {
    FlatValue *cur=(FlatValue*)malloc(JQ_MAX_VALUES*sizeof(FlatValue));
    FlatValue *next=(FlatValue*)malloc(JQ_MAX_VALUES*sizeof(FlatValue));
    int cn=1,si,i,j,nn,wo=0;
    if(!cur||!next) { free(cur); free(next); return -1; }
    cur[0]=*root;
    for(si=0;si<p->n;si++) {
        JqSeg*g=(JqSeg*)&p->s[si]; nn=0;
        if(g->k==JS_LISTMARK) { for(i=0;i<cn;i++) if(cur[i].tag==VAL_LIST&&nn<JQ_MAX_VALUES) next[nn++]=cur[i]; }
        else for(i=0;i<cn;i++) {
            FlatValue v=cur[i],got;
            if(g->k==JS_KEY&&v.tag==VAL_OBJECT) { if(flat_obj_get(c,v.u.coll_id,g->key,&got)==0&&nn<JQ_MAX_VALUES) next[nn++]=got; }
            else if((g->k==JS_INDEX)&&v.tag==VAL_LIST) { if(flat_list_get(c,v.u.coll_id,g->ix,&got)==0&&nn<JQ_MAX_VALUES) next[nn++]=got; }
            else if((g->k==JS_STAR||g->k==JS_AT)&&v.tag==VAL_LIST) {
                int len=0,ix=-1; flat_list_len(c,v.u.coll_id,&len);
                if(ctx && wo<ctx->n) ix=ctx->idx[wo];
                if(ix>=0) { if(flat_list_get(c,v.u.coll_id,ix,&got)==0&&nn<JQ_MAX_VALUES) next[nn++]=got; }
                else for(j=0;j<len&&nn<JQ_MAX_VALUES;j++) if(flat_list_get(c,v.u.coll_id,j,&got)==0) next[nn++]=got;
            }
        }
        if(g->k==JS_STAR||g->k==JS_AT) wo++;
        cn=nn; if(!cn) break; memcpy(cur,next,(size_t)cn*sizeof(FlatValue));
    }
    if(outn) *outn=cn;
    if(collect&&out) memcpy(out,cur,(size_t)cn*sizeof(FlatValue));
    free(cur); free(next);
    return cn?0:-1;
}
static int jq_cmp(const FlatValue*v,const char*op,const FlatValue*r,const FlatStringPool*p) {
    if(!strcmp(op,"=")) return jq_eq(v,r,p);
    if(!strcmp(op,"!=")) return !jq_eq(v,r,p);
    if(v->tag!=VAL_NUMBER||r->tag!=VAL_NUMBER) return 0;
    { double a=flat_number_as_f64(v),b=flat_number_as_f64(r);
      if(!strcmp(op,">"))return a>b;
      if(!strcmp(op,"<"))return a<b;
      if(!strcmp(op,">="))return a>=b;
      if(!strcmp(op,"<="))return a<=b; }
    return 0;
}
static int jq_type(const FlatValue*v,const char*t) {
    if(!strcasecmp(t,"number"))return v->tag==VAL_NUMBER;
    if(!strcasecmp(t,"string"))return v->tag==VAL_STR_INLINE||v->tag==VAL_STR_REF;
    if(!strcasecmp(t,"bool")||!strcasecmp(t,"boolean"))return v->tag==VAL_BOOL;
    if(!strcasecmp(t,"null"))return v->tag==VAL_NULL;
    if(!strcasecmp(t,"list")||!strcasecmp(t,"array"))return v->tag==VAL_LIST;
    if(!strcasecmp(t,"object"))return v->tag==VAL_OBJECT;
    return 0;
}
static int jq_eval(const JqAst*a,int id,const FlatValue*root,FlatNativeCtx*ctx,const JqContext*wc) {
    const JqNode*n=&a->n[id]; FlatValue *vals; int vn=0,i,j,hit=0;
    if(n->k==JN_AND)return jq_eval(a,n->a,root,ctx,wc)&&jq_eval(a,n->b,root,ctx,wc);
    if(n->k==JN_OR)return jq_eval(a,n->a,root,ctx,wc)||jq_eval(a,n->b,root,ctx,wc);
    if(n->k==JN_NOT)return !jq_eval(a,n->a,root,ctx,wc);
    vals=(FlatValue*)malloc(JQ_MAX_VALUES*sizeof(FlatValue));
    if(!vals) return 0;
    if(jq_path_values(root,&n->path,wc,ctx->coll,vals,&vn,1)!=0) { free(vals); return 0; }
    for(i=0;i<vn;i++) {
        FlatValue*v=&vals[i]; int ok=0;
        switch(n->k) {
        case JN_EXISTS: ok=1; break;
        case JN_CMP: ok=jq_cmp(v,n->op,&n->vals[0],ctx->pool); break;
        case JN_BETWEEN: ok=v->tag==VAL_NUMBER&&flat_number_as_f64(v)>=n->x&&flat_number_as_f64(v)<=n->y; break;
        case JN_IS:
            if(!strcmp(n->word,"null")) ok=n->flag?v->tag!=VAL_NULL:v->tag==VAL_NULL;
            else if(!strcmp(n->word,"true")) ok=v->tag==VAL_BOOL&&v->u.boolean;
            else ok=v->tag==VAL_BOOL&&!v->u.boolean;
            break;
        case JN_LEN: {
            int len=-1; char b[4096];
            if(n->flag&&v->tag==VAL_LIST) flat_list_len(ctx->coll,v->u.coll_id,&len);
            else if(!n->flag&&(v->tag==VAL_STR_INLINE||v->tag==VAL_STR_REF)&&jq_getstr(v,ctx->pool,b,sizeof b)==0) len=(int)strlen(b);
            if(len>=0) { double x=n->x; if(!strcmp(n->op,"="))ok=len==x;else if(!strcmp(n->op,"!="))ok=len!=x;else if(!strcmp(n->op,">"))ok=len>x;else if(!strcmp(n->op,"<"))ok=len<x;else if(!strcmp(n->op,">="))ok=len>=x;else if(!strcmp(n->op,"<="))ok=len<=x; }
            break;
        }
        case JN_IN: for(j=0;j<n->val_n;j++) if(jq_eq(v,&n->vals[j],ctx->pool)){ok=1;break;} break;
        case JN_HAS: if(v->tag==VAL_LIST) { int len=0,found=0;flat_list_len(ctx->coll,v->u.coll_id,&len);
            if(n->flag) { ok=1; for(j=0;j<n->val_n;j++){int f=0;for(int k=0;k<len;k++){FlatValue q;if(flat_list_get(ctx->coll,v->u.coll_id,k,&q)==0&&jq_eq(&q,&n->vals[j],ctx->pool)){f=1;break;}}if(!f){ok=0;break;}} }
            else { for(j=0;j<n->val_n;j++)for(int k=0;k<len;k++){FlatValue q;if(flat_list_get(ctx->coll,v->u.coll_id,k,&q)==0&&jq_eq(&q,&n->vals[j],ctx->pool))found=1;} ok=found; } } break;
        case JN_CONTAINS: case JN_STARTS: case JN_ENDS: if(v->tag==VAL_STR_INLINE||v->tag==VAL_STR_REF) {
            char b[4096],needle[256]; jq_getstr(v,ctx->pool,b,sizeof b); jq_getstr(&n->vals[0],ctx->pool,needle,sizeof needle);
            if(n->k==JN_CONTAINS)ok=strstr(b,needle)!=NULL;else if(n->k==JN_STARTS)ok=strncmp(b,needle,strlen(needle))==0;else {size_t bl=strlen(b),nl=strlen(needle);ok=bl>=nl&&strcmp(b+bl-nl,needle)==0;}
        } break;
        case JN_TYPE: ok=jq_type(v,n->word); break;
        default: break;
        }
        if(ok) { hit=1; break; }
    }
    if(n->negate && (n->k==JN_IN||n->k==JN_HAS||n->k==JN_CONTAINS||
                     n->k==JN_STARTS||n->k==JN_ENDS||n->k==JN_TYPE)) return !hit;
    return hit;
}

static int jq_split_args(char *s,char **out,int want) {
    int d=0,q=0,esc=0,k=0; char qc=0; char *p,*start=s;
    for(p=s;;p++) {
        char ch=*p;
        if(q){if(!ch)return -1;if(esc)esc=0;else if(ch=='\\')esc=1;else if(ch==qc)q=0;continue;}
        if(ch=='"'||ch=='\'') { q=1; qc=ch; continue; }
        if(ch=='['||ch=='(')d++;else if(ch==']'||ch==')'){if(d)d--;}
        if((ch==','&&d==0)||ch=='\0') {
            if(k>=want)return -1;
            if(ch)*p='\0';
            out[k++]=flat_native_trim(start);
            if(!ch)break;
            start=p+1;
        }
    }
    return k==want?0:-1;
}
static int jq_get_text_arg(FlatNativeCtx *ctx,const char *expr,char *out,size_t cap) {
    FlatValue v; size_t n=0; const uint8_t *p;
    if(flat_native_parse_expr(ctx,expr,&v)!=0) return -1;
    if(v.tag==VAL_STR_INLINE) { if(v.u.ins.len>=cap)return -1; memcpy(out,v.u.ins.bytes,v.u.ins.len); out[v.u.ins.len]=0; return 0; }
    if(v.tag==VAL_STR_REF) { p=flat_pool_read(ctx->pool,v.u.ref_id,&n); if(!p||n>=cap)return -1; memcpy(out,p,n); out[n]=0; return 0; }
    return -1;
}
typedef struct { FlatValue v; JqContext c; } JqCandidate;
static void jq_collect_at(const FlatValue *cur,const JqPath *p,int si,const JqContext *wc,
                          FlatNativeCtx *ctx,JqCandidate *out,int *n) {
    if(*n>=JQ_MAX_VALUES)return;
    if(si>=p->n){out[*n].v=*cur;out[*n].c=*wc;(*n)++;return;}
    {
        JqSeg *g=(JqSeg*)&p->s[si]; FlatValue v; int i,len;
        if(g->k==JS_KEY) {
            if(cur->tag==VAL_OBJECT&&flat_obj_get(ctx->coll,cur->u.coll_id,g->key,&v)==0)
                jq_collect_at(&v,p,si+1,wc,ctx,out,n);
        } else if(g->k==JS_INDEX) {
            if(cur->tag==VAL_LIST&&flat_list_get(ctx->coll,cur->u.coll_id,g->ix,&v)==0)
                jq_collect_at(&v,p,si+1,wc,ctx,out,n);
        } else if(g->k==JS_AT||g->k==JS_STAR) {
            if(cur->tag!=VAL_LIST)return;
            flat_list_len(ctx->coll,cur->u.coll_id,&len);
            for(i=0;i<len&&*n<JQ_MAX_VALUES;i++) {
                JqContext next=*wc;
                if(next.n<JQ_MAX_SEGS)next.idx[next.n++]=i;
                if(flat_list_get(ctx->coll,cur->u.coll_id,i,&v)==0)
                    jq_collect_at(&v,p,si+1,&next,ctx,out,n);
            }
        } else if(g->k==JS_LISTMARK) {
            if(cur->tag==VAL_LIST)jq_collect_at(cur,p,si+1,wc,ctx,out,n);
        }
    }
}
static int jq_path_wildcards(const JqPath *p) {
    int i,n=0; for(i=0;i<p->n;i++) if(p->s[i].k==JS_STAR||p->s[i].k==JS_AT) n++;
    return n;
}
static int jq_find_count_path(const JqAst*a,int id,JqPath*out,int *best) {
    const JqNode*n=&a->n[id];
    if(n->k==JN_AND||n->k==JN_OR) {
        jq_find_count_path(a,n->a,out,best);
        jq_find_count_path(a,n->b,out,best);
        return 0;
    }
    if(n->k==JN_NOT) return jq_find_count_path(a,n->a,out,best);
    if(jq_path_wildcards(&n->path)>*best) { *out=n->path; *best=jq_path_wildcards(&n->path); }
    return 0;
}
static int jq_run(FlatNativeCtx *ctx,int mode) {
    char buf[8192],*parts[3],query[4096],ret[1024];
    FlatValue root; JqAst *ast=NULL; JqPath rp,fp; JqCandidate *cand=NULL; FlatValue *results=NULL;
    int cn=0,i,matches=0,best=0; uint32_t lid;
    if(!ctx->out||!ctx->io_status||!ctx->coll||!ctx->inner){return -1;}
    *ctx->io_status=FLAT_IO_OK;
    ast=(JqAst*)calloc(1,sizeof(*ast));
    cand=(JqCandidate*)calloc(JQ_MAX_VALUES,sizeof(*cand));
    results=(FlatValue*)calloc(JQ_MAX_VALUES,sizeof(*results));
    if(!ast||!cand||!results) goto fail;
    snprintf(buf,sizeof(buf),"%s",ctx->inner);
    if(jq_split_args(buf,parts,mode==1?3:2)!=0 ||
       flat_native_parse_rhs(ctx,parts[0],&root)!=0) goto fail;
    if(root.tag!=VAL_OBJECT&&root.tag!=VAL_LIST) goto fail_root;
    if(jq_get_text_arg(ctx,parts[1],query,sizeof(query))!=0||jq_parse(query,ast)!=0) goto fail_root;
    if(mode==0) {
        int ok=jq_eval(ast,ast->root,&root,ctx,NULL);
        flat_native_parse_rhs_release(ctx,&root);
        free(ast); free(cand); free(results);
        return flat_value_set_bool(ctx->out,ok);
    }
    if(mode==1) {
        if(jq_get_text_arg(ctx,parts[2],ret,sizeof(ret))!=0) goto fail_root;

    }
    if(mode==2) {
        if(jq_find_count_path(ast,ast->root,&fp,&best)!=0) goto fail_root;
        /* Count candidates at the deepest wildcard-bearing path. */
        rp=fp; for(i=0;i<rp.n;i++) if(rp.s[i].k==JS_STAR) rp.s[i].k=JS_AT;
    } else {

    }
    if(mode==1) {
        JqLex lex;
        if(jq_lex(ret,&lex)!=0 || jq_path(&lex,&rp,1)!=0 || !jq_is(&lex,JT_EOF)) goto fail_root;
        if(rp.n==0) {
            if(jq_eval(ast,ast->root,&root,ctx,NULL)) results[matches++]=root;
        } else {
            jq_collect_at(&root,&rp,0,&(JqContext){{0},0},ctx,cand,&cn);
            for(i=0;i<cn;i++) if(jq_eval(ast,ast->root,&root,ctx,&cand[i].c)) results[matches++]=cand[i].v;
        }
    } else {
        jq_collect_at(&root,&rp,0,&(JqContext){{0},0},ctx,cand,&cn);
        for(i=0;i<cn;i++) if(jq_eval(ast,ast->root,&root,ctx,&cand[i].c)) matches++;
    }
    if(mode==2) {
        flat_native_parse_rhs_release(ctx,&root);
        free(ast); free(cand); free(results);
        return flat_value_set_number(ctx->out,(double)matches);
    }
    if(matches==0) {
        if(flat_list_empty(ctx->coll,ctx->pool,&lid)!=0) {
            flat_native_parse_rhs_release(ctx,&root);
            goto fail;
        }
    } else if(flat_list_build(ctx->coll,ctx->pool,results,matches,&lid)!=0) {
        flat_native_parse_rhs_release(ctx,&root);
        goto fail;
    }
    flat_native_parse_rhs_release(ctx,&root);
    free(ast); free(cand); free(results);
    return flat_value_set_list(ctx->out,lid);
fail_root:
    flat_native_parse_rhs_release(ctx,&root);
fail:
    free(ast); free(cand); free(results);
    *ctx->io_status=FLAT_IO_ERR; return -1;
}
int flat_native_json_check(FlatNativeCtx *ctx) { return jq_run(ctx,0); }
int flat_native_json_find(FlatNativeCtx *ctx) { return jq_run(ctx,1); }
int flat_native_json_count(FlatNativeCtx *ctx) { return jq_run(ctx,2); }
