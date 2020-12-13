#include <assert.h>
#include "mgpriv.h"
#include "ggen.h"
#include "gfa-priv.h"
#include "algo.h"

int32_t mg_gc_index(void *km, int min_mapq, int min_map_len, int min_depth_len, const gfa_t *g, int32_t n_seq, mg_gchains_t *const* gcs,
					double *a_dens, int32_t **soff_, int32_t **qoff_, mg_intv_t **sintv_, mg_intv_t **qintv_);

typedef struct {
	int32_t bid;
	uint8_t is_stem:4, is_src:4;
} callaux_t;

typedef struct {
	int32_t t, i;
	int32_t st, en, strand;
} bbaux_t;

void mg_call_asm(const gfa_t *g, int32_t n_seq, const mg_bseq1_t *seq, mg_gchains_t *const *gcs, int32_t min_mapq, int32_t min_blen)
{
	int32_t i, j, t, max_acnt, *soff, *qoff, n_bb, m_ovlp = 0, *ovlp = 0;
	mg_intv_t *sintv, *qintv;
	double a_dens;
	gfa_bubble_t *bb;
	callaux_t *ca;
	bbaux_t *ba;
	kstring_t out = {0,0,0};

	max_acnt = mg_gc_index(0, min_mapq, min_blen>>1, min_blen, g, n_seq, gcs, &a_dens, &soff, &qoff, &sintv, &qintv);
	if (max_acnt == 0) return;

	bb = gfa_bubble(g, &n_bb);
	GFA_CALLOC(ba, n_bb);
	GFA_CALLOC(ca, g->n_seg);
	for (i = 0; i < n_bb; ++i) {
		gfa_bubble_t *b = &bb[i];
		assert(b->n_seg >= 2);
		for (j = 0; j < b->n_seg; ++j)
			ca[b->v[j]>>1].bid = i;
		ca[b->v[0]>>1].is_stem = ca[b->v[b->n_seg-1]>>1].is_stem = 1;
		ca[b->v[0]>>1].is_src = 1;
	}

	for (t = 0; t < n_seq; ++t) {
		const mg_gchains_t *gt = gcs[t];
		for (i = 0; i < gt->n_gc; ++i) {
			const mg_gchain_t *gc = &gt->gc[i];
			int32_t st = -1;
			for (j = 1; j < gc->cnt; ++j) {
				const mg_llchain_t *lc = &gt->lc[gc->off + j];
				if (!ca[lc->v>>1].is_stem && ca[(lc-1)->v>>1].is_stem) {
					st = gc->off + j;
				} else if (ca[lc->v>>1].is_stem && !ca[(lc-1)->v>>1].is_stem && st > 0) {
					int32_t n_ovlp, k, en = gc->off + j, qs, qe, span, bid, strand;
					bbaux_t *p;
					assert(en > st);
					// test overlap on the query
					span = gt->a[gt->lc[st].off].y >> 32 & 0xff;
					qs = (int32_t)gt->a[gt->lc[st].off].y + 1 - span;
					qe = (int32_t)gt->a[gt->lc[en - 1].off].y + 1;
					n_ovlp = mg_intv_overlap(0, qoff[t+1] - qoff[t], &qintv[qoff[t]], qs, qe, &ovlp, &m_ovlp);
					if (n_ovlp > 1) continue; // overlap on the query - not orthologous
					// test overlap on the graph
					for (k = st; k < en; ++k) {
						const mg_llchain_t *lk = &gt->lc[k];
						int32_t seg = lk->v>>1;
						n_ovlp = mg_intv_overlap(0, soff[seg+1] - soff[seg], &sintv[soff[seg]], 0, g->seg[seg].len, &ovlp, &m_ovlp);
						if (n_ovlp > 1) break; // overlap on the graph - not orthoologous
					}
					if (k < en) continue;
					// attach to the bubble
					assert(ca[gt->lc[st-1].v>>1].is_stem && ca[gt->lc[en].v>>1].is_stem);
					if (ca[gt->lc[st-1].v>>1].bid < ca[gt->lc[en].v>>1].bid)
						bid = ca[gt->lc[st-1].v>>1].bid, strand = 1;
					else bid = ca[gt->lc[en].v>>1].bid, strand = -1;
					for (k = st; k < en; ++k) // sanity check
						if (ca[gt->lc[k].v>>1].bid != bid)
							break;
					assert(k == en);
					p = &ba[bid];
					p->t = t, p->i = i, p->st = st, p->en = en, p->strand = strand;
				}
			}
		}
	}

	for (i = 0; i < n_bb; ++i) {
		gfa_bubble_t *b = &bb[i];
		bbaux_t *a = &ba[i];
		const mg_gchains_t *gt = gcs[a->t];
		int32_t st = -1, en = -1, len = 0, span;
		out.l = 0;
		mg_sprintf_lite(&out, "%s\t%d\t%d\t%c%s\t%c%s\t", g->sseq[b->snid].name, b->ss, b->se, "><"[b->v[0]&1], g->seg[b->v[0]>>1].name,
						"><"[b->v[b->n_seg-1]&1], g->seg[b->v[b->n_seg-1]>>1].name);
		for (j = a->st; j < a->en; ++j)
			len += g->seg[gcs[a->t]->lc[j].v>>1].len;
		if (a->st == a->en) {
			mg_sprintf_lite(&out, "*");
		} else if (ba->strand > 0) {
			for (j = a->st; j < a->en; ++j)
				mg_sprintf_lite(&out, "%c%s", "><"[gt->lc[j].v&1], g->seg[gt->lc[j].v>>1].name);
		} else {
			for (j = a->en - 1; j >= a->st; --j)
				mg_sprintf_lite(&out, "%c%s", "<>"[gt->lc[j].v&1], g->seg[gt->lc[j].v>>1].name);
		}
		mg_sprintf_lite(&out, ":%d:%c:%s", len, a->strand > 0? '+' : '-', seq[a->t].name);
		span = gt->a[gt->lc[a->st - 1].off].y >> 32 & 0xff;
		st = (int32_t)gt->a[gt->lc[a->st - 1].off + gt->lc[a->st - 1].cnt - 1].y + 1 - span;
		en = (int32_t)gt->a[gt->lc[a->en].off].y + 1;
		mg_sprintf_lite(&out, ":%d:%d", st, en);
		puts(out.s);
	}

	free(ba); free(ca);
	free(soff); free(qoff); free(sintv); free(qintv);
	for (i = 0; i < n_bb; ++i) free(bb[i].v);
	free(bb);
	free(out.s);
}