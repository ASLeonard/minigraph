#include <assert.h>
#include <string.h>
#include "mgpriv.h"
#include "kalloc.h"

static void append_cigar1(void *km, mg32_v *c, int32_t op, int32_t len)
{
	if (c->n > 0 && (c->a[c->n - 1]&0xf) == op) {
		c->a[c->n - 1] += len<<4;
	} else {
		if (c->n == c->m) {
			c->m += (c->m>>1) + 16;
			KREALLOC(km, c->a, c->m);
		}
		c->a[c->n++] = len<<4 | op;
	}
}

static void append_cigar(void *km, mg32_v *c, int32_t n_cigar, const uint32_t *cigar)
{
	if (n_cigar == 0) return;
	append_cigar1(km, c, cigar[0]&0xf, cigar[0]>>4);
	if (c->n + n_cigar - 1 > c->m) {
		c->m = c->n + n_cigar - 1;
		kroundup32(c->m);
		KREALLOC(km, c->a, c->m);
	}
	memcpy(&c->a[c->n], &cigar[1], sizeof(*cigar) * (n_cigar - 1));
	c->n += n_cigar - 1;
}

void mg_gchain_cigar(void *km, const gfa_t *g, const gfa_edseq_t *es, const char *qseq, mg_gchains_t *gt)
{
	int32_t i, l_seq = 0, m_seq = 0;
	char *seq = 0;
	mg32_v cigar = {0,0,0};
	for (i = 0; i < gt->n_gc; ++i) {
		mg_gchain_t *gc = &gt->gc[i];
		int32_t l0 = gc->off;
		int32_t off_a0 = gt->lc[l0].off;
		int32_t j, j0 = 0, k, l;
		cigar.n = 0;
		append_cigar1(km, &cigar, 7, gt->a[off_a0].y>>32&0xff);
		for (j = 1; j < gc->n_anchor; ++j) {
			const mg128_t *q, *p = &gt->a[off_a0 + j];
			if ((p->y & MG_SEED_IGNORE) && j != gc->n_anchor - 1) continue;
			q = &gt->a[off_a0 + j0];
			// find the lchain that contains the anchor
			for (l = l0; l < gc->off + gc->cnt; ++l) {
				mg_llchain_t *r = &gt->lc[l];
				if (off_a0 + j >= r->off && off_a0 + j < r->off + r->cnt)
					break;
			}
			assert(l < gc->off + gc->cnt);
			assert((int32_t)q->x < g->seg[gt->lc[l0].v>>1].len);
			// calculate the target sequence length
			if (l == l0) {
				l_seq = (int32_t)p->x - (int32_t)q->x;
			} else {
				l_seq = g->seg[gt->lc[l0].v>>1].len - (int32_t)q->x - 1;
				for (k = l0 + 1; k < l; ++k)
					l_seq += es[gt->lc[k].v].len;
				l_seq += (int32_t)p->x + 1;
			}
			if (l_seq + 1 > m_seq) {
				m_seq = l_seq + 1;
				kroundup32(m_seq);
				KREALLOC(km, seq, m_seq);
			}
			// get the target sequence
			if (l == l0) { // on the same vertex
				memcpy(seq, &es[gt->lc[l0].v].seq[(int32_t)q->x + 1], l_seq);
			} else {
				uint32_t v = gt->lc[l0].v;
				l_seq = g->seg[v>>1].len - (int32_t)q->x - 1;
				memcpy(seq, &es[v].seq[(int32_t)q->x + 1], l_seq);
				for (k = l0 + 1; k < l; ++k) {
					v = gt->lc[k].v;
					memcpy(&seq[l_seq], es[v].seq, es[v].len);
					l_seq += es[v].len;
				}
				memcpy(&seq[l_seq], es[gt->lc[l].v].seq, (int32_t)p->x + 1);
				l_seq += (int32_t)p->x + 1;
				#if 0
				printf("===> %d:%d [%d,%d) %d:%d <===\n", l0, l, (int32_t)q->y+1, (int32_t)p->y+1, l_seq, (int32_t)p->y - (int32_t)q->y);
				for (k = 0; k < l_seq; ++k) putchar(seq[k]); putchar('\n');
				for (k = 0; k < (int32_t)p->y - (int32_t)q->y; ++k) putchar(qseq[(int32_t)q->y + 1 + k]); putchar('\n');
				#endif
			}
			{
				int32_t n_cigar, ed, qlen = (int32_t)p->y - (int32_t)q->y;
				uint32_t *ci;
				const char *qs = &qseq[(int32_t)q->y + 1];
				ci = lv_ed_global_cigar(km, l_seq, seq, qlen, qs, &ed, &n_cigar);
				append_cigar(km, &cigar, n_cigar, ci);
				kfree(km, ci);
			}
			j0 = j, l0 = l;
		}
		// save the CIGAR to gt->gc[i]
		gc->p = (mg_cigar_t*)kcalloc(gt->km, 1, cigar.n * 4 + sizeof(mg_cigar_t));
		gc->p->n_cigar = cigar.n;
		memcpy(gc->p->cigar, cigar.a, cigar.n * 4);
		for (j = 0, l = 0; j < gc->p->n_cigar; ++j) {
			int32_t op = gc->p->cigar[j]&0xf, len = gc->p->cigar[j]>>4;
			if (op == 7) gc->p->mlen += len, gc->p->blen += len;
			else gc->p->blen += len;
			if (op != 1) gc->p->aplen += len;
			if (op != 2) l += len;
		}
		assert(l == gc->qe - gc->qs && gc->p->aplen == gc->pe - gc->ps);
	}
	kfree(km, seq);
	kfree(km, cigar.a);
}