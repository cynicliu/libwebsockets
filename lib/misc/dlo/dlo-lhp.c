/*
 * lws abstract display
 *
 * Copyright (C) 2019 - 2022 Andy Green <andy@warmcat.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Display List LHP layout
 */

#include <private-lib-core.h>
#include "private-lib-drivers-display-dlo.h"

/*
 * Newline moves the psb->cury to cover text that was already placed using the
 * old psb->cury as to top of it.  So a final newline on the last line of text
 * does not create an extra blank line.
 */

static const lws_fx_t two = { 2,0 };

static void
newline(lhp_ctx_t *ctx, lhp_pstack_t *psb, lhp_pstack_t *ps,
	lws_displaylist_t *dl)
{
	int16_t group_baseline = 9999, group_height = 0;
	lws_fx_t line_height = { 0, 0 }, w, add, ew, t1;
	const struct lcsp_atr *a;
	lws_dlo_t *dlo, *d, *d1;
	int t = 0;

	if (!psb || !ps) {
		lwsl_err("%s: psb/ps NULL!\n", __func__);
		return;
	}

	dlo = (lws_dlo_t *)psb->opaque1;

	lws_fx_add(&w, lws_csp_px(ps->css_padding[CCPAS_LEFT], ps),
		       lws_csp_px(ps->css_padding[CCPAS_RIGHT], ps));

	if (lws_fx_comp(&w, &psb->widest) > 0)
		psb->widest = w;

	if (!dlo || !dlo->children.tail)
		return;

	d = lws_container_of(dlo->children.tail, lws_dlo_t, list);

	/*
	 * We may be at the end of a line of text
	 *
	 * Figure out the biggest height on the line, and the total width
	 */

	while (d) {
		t |= d->_destroy == lws_display_dlo_text_destroy;
		/* find the "worst" height on the line */
		if (lws_fx_comp(&d->box.h, &line_height) > 0)
			line_height = d->box.h;

		if (d->_destroy == lws_display_dlo_text_destroy) {
			lws_dlo_text_t *text = lws_container_of(d,
						lws_dlo_text_t, dlo.list);

			if (text->font_y_baseline < group_baseline)
				group_baseline = text->font_y_baseline;
			if (text->font_height > group_height)
				group_height = text->font_height;
		}

		if (!d->flag_runon)
			break;
		d = lws_container_of(d->list.prev, lws_dlo_t, list);
	};

	/* mark the related text dlos with information about group bl and h,
	 * offset box y to align to group baseline if necessary */

	d1 = d;
	while (d) {
		if (d->_destroy == lws_display_dlo_text_destroy) {
			lws_dlo_text_t *t1 = lws_container_of(d1,
						lws_dlo_text_t, dlo.list);
			lws_fx_t ft;

			t1->group_height = group_height;
			t1->group_y_baseline = group_baseline;

			ft.whole = (t1->font_height - t1->font_y_baseline) -
					(group_height - group_baseline);
			ft.frac = 0;

			lws_fx_sub(&t1->dlo.box.y,  &t1->dlo.box.y, &ft);
		}
		if (!d1->list.next)
			break;
		d1 = lws_container_of(d1->list.next, lws_dlo_t, list);
	};

	w = psb->curx;
	ew = ctx->ic.wh_px[0];
	if (psb->css_width && psb->css_width->unit != LCSP_UNIT_NONE)
		ew = *lws_csp_px(psb->css_width, psb);
	lws_fx_sub(&ew, &ew, lws_csp_px(ps->css_margin[CCPAS_RIGHT], ps));
	lws_fx_sub(&ew, &ew, lws_csp_px(ps->css_padding[CCPAS_RIGHT], ps));

	if (lws_fx_comp(&w, &psb->widest) > 0)
		psb->widest = w;

	if (!t) /* no textual children to newline (eg, <div></div>) */
		return;

	 /*
	  * now is our chance to fix up dlos that are part of the line for
	  * text-align rule of the container.
	  */

	a = lws_css_cascade_get_prop_atr(ctx, LCSP_PROP_TEXT_ALIGN);
	if (a) {

		switch (a->propval) {
		case LCSP_PROPVAL_CENTER:
			add = *lws_csp_px(ps->css_padding[CCPAS_LEFT], ps);
			lws_fx_sub(&t1, &ew, &w);
			lws_fx_div(&t1, &t1, &two);
			lws_fx_add(&add, &add, &t1);
			goto fixup;
		case LCSP_PROPVAL_RIGHT:
			lws_fx_sub(&add, &ew, &w);
			lws_fx_sub(&add, &add, &d->box.x);

fixup:
			lws_fx_add(&t1, &add, &w);
			if (lws_fx_comp(&t1, &psb->widest) > 0)
				psb->widest = t1;

			do {
				lws_fx_add(&d->box.x, &d->box.x, &add);
				if (!d->list.next)
					break;
				d = lws_container_of(d->list.next, lws_dlo_t,
							list);
			} while (1);
			break;
		default:
			break;
		}
	}

	lws_fx_add(&psb->cury, &psb->cury, &line_height);
	lws_fx_set(psb->curx, 0, 0);
	psb->runon = 0;
}

static void
runon(lhp_pstack_t *ps, lws_dlo_t *dlo)
{
	dlo->flag_runon = (uint8_t)(ps->runon & 1);
	ps->runon = 1;
}

/*
 * Generic LHP displaylist object layout callback... converts html elements
 * into DLOs on the display list
 */

lws_stateful_ret_t
lhp_displaylist_layout(lhp_ctx_t *ctx, char reason)
{
	lhp_pstack_t *psb = NULL, *ps = lws_container_of(ctx->stack.tail,
							 lhp_pstack_t, list);
	struct lws_context *cx = (struct lws_context *)ctx->user1;
	lws_dl_rend_t *drt = (lws_dl_rend_t *)ctx->user;
	const lws_display_font_t *f = NULL;
	lws_fx_t br[4], t1, indent;
	uint32_t col = 0xff000000;
	lws_dlo_rect_t *rect;
	lws_dlo_text_t *txt;
	const lcsp_atr_t *a;
	lws_dlo_rect_t *r;
	lws_dlo_image_t u;
	lws_fx_t w, h;
	lws_box_t box;
	char url[128];
	int n, s = 0;

	/* default font choice */
	lws_font_choice_t fc = {
		.family_name		= "term, serif",
		.fixed_height		= 16,
		.weight			= 400,
	};

	if (!ps->opaque2) {
		a = lws_css_cascade_get_prop_atr(ctx, LCSP_PROP_FONT_SIZE);
		if (a)
			fc.fixed_height = (uint16_t)a->u.i.whole;

		a = lws_css_cascade_get_prop_atr(ctx, LCSP_PROP_FONT_FAMILY);
		if (a)
			fc.family_name = (const char *)&a[1];

		a = lws_css_cascade_get_prop_atr(ctx, LCSP_PROP_FONT_WEIGHT);
		if (a) {
			switch (a->propval) {
			case LCSP_PROPVAL_BOLD:
				fc.weight = 700;
				break;
			case LCSP_PROPVAL_BOLDER:
				fc.weight = 800;
				break;
			default:
				if (a->u.i.whole)
					fc.weight = (uint16_t)a->u.i.whole;
				break;
			}
		}

		ps->opaque2 = lws_font_choose(cx, &fc);
	}
	f = (const lws_display_font_t *)ps->opaque2;

	psb = lws_css_get_parent_block(ctx, ps);

	switch (reason) {
	case LHPCB_CONSTRUCTED:
	case LHPCB_DESTRUCTED:
	case LHPCB_COMPLETE:
	case LHPCB_FAILED:
		break;
	case LHPCB_ELEMENT_START:
		if (ctx->npos == 2 && !strncmp(ctx->buf, "br", 2))
			newline(ctx, psb, ps, drt->dl);

		if (ctx->npos == 3 && !strncmp(ctx->buf, "div", 3)) {

			lws_fx_set(box.x, 0, 0);
			lws_fx_set(box.y, 0, 0);
			lws_fx_set(box.h, 0, 0);
			lws_fx_set(box.w, 0, 0);

			br[0] = *lws_csp_px(ps->css_border_radius[0], ps);
			br[1] = *lws_csp_px(ps->css_border_radius[1], ps);
			br[2] = *lws_csp_px(ps->css_border_radius[2], ps);
			br[3] = *lws_csp_px(ps->css_border_radius[3], ps);

			if (ps->css_position->propval == LCSP_PROPVAL_ABSOLUTE) {
				box.x = *lws_csp_px(ps->css_pos[CCPAS_LEFT], ps);
				box.y = *lws_csp_px(ps->css_pos[CCPAS_TOP], ps);
			} else {
				if (psb) {
				/* margin adjusts our child box origin */
				lws_fx_add(&box.x, &psb->curx,
					   lws_csp_px(psb->css_margin[CCPAS_LEFT], psb));
				lws_fx_add(&box.y, &psb->cury,
					   lws_csp_px(psb->css_margin[CCPAS_TOP], psb));
				}
			}

			/* padding adjusts our child curx/y */
			lws_fx_add(&ps->curx, &ps->curx,
				   lws_csp_px(ps->css_padding[CCPAS_LEFT], ps));
			lws_fx_add(&ps->cury, &ps->cury,
				   lws_csp_px(ps->css_padding[CCPAS_TOP], ps));

			/* width and height will get reassessed at end of div */
			box.h = ctx->ic.wh_px[LWS_LHPREF_HEIGHT];
			lws_fx_sub(&box.w,
				   &ctx->ic.wh_px[LWS_LHPREF_WIDTH], &box.x);

			if (ps->css_width &&
			    ps->css_width->unit != LCSP_UNIT_NONE &&
			    lws_fx_comp(lws_csp_px(ps->css_width, ps), &box.w) < 0)
				box.w = *lws_csp_px(ps->css_width, ps);

			lws_fx_add(&box.w, &box.w,
				   lws_csp_px(ps->css_padding[CCPAS_LEFT], ps));
			lws_fx_add(&box.w, &box.w,
				   lws_csp_px(ps->css_padding[CCPAS_RIGHT], ps));

			ps->drt.w = box.w;
			ps->curx = *lws_csp_px(ps->css_padding[CCPAS_LEFT], ps);
			ps->cury = *lws_csp_px(ps->css_padding[CCPAS_TOP], ps);

			r = lws_display_dlo_rect_new(drt->dl,
				(lws_dlo_t *)(psb ? psb->opaque1 : NULL), &box, br,
				ps->css_background_color ?
				  ps->css_background_color->u.rgba : 0);
			if (!r)
				return LWS_SRET_FATAL;

			lws_lhp_tag_dlo_id(ctx, ps, &r->dlo);

			ps->opaque1 = r; /* for fixing up size at end of div */
		}

		if (ctx->npos == 3 && !strncmp(ctx->buf, "img", 3) && psb) {
			const char *pname = lws_html_get_atr(ps, "src", 3);

			if (ps->css_position->propval == LCSP_PROPVAL_ABSOLUTE) {
				box.x = *lws_csp_px(ps->css_pos[CCPAS_LEFT], ps);
				box.y = *lws_csp_px(ps->css_pos[CCPAS_TOP], ps);
			} else {
				box.x = psb->curx;
				box.y = psb->cury;
			}

			lws_fx_set(box.x, 0, 0);
			lws_fx_set(box.y, 0, 0);

			lws_fx_add(&box.x, &box.x,
				lws_csp_px(psb->css_margin[CCPAS_LEFT], psb));
			lws_fx_add(&box.y, &box.y,
				lws_csp_px(psb->css_margin[CCPAS_TOP], psb));

			box.h = ctx->ic.wh_px[1]; /* placeholder */
			lws_fx_sub(&box.w, &ctx->ic.wh_px[0], &box.x);

			if (ps->css_width &&
			    lws_fx_comp(lws_csp_px(ps->css_width, ps), &box.w) > 0)
				box.w = *lws_csp_px(ps->css_width, ps);

			if (lws_http_rel_to_url(url, sizeof(url),
						ctx->base_url, pname))
				break;

			if (lws_dlo_ss_find(cx, url, &u))
				break;

			lws_lhp_tag_dlo_id(ctx, ps, (lws_dlo_t *)(u.u.dlo_jpeg));

			w = *lws_csp_px(lws_css_cascade_get_prop_atr(ctx,
							LCSP_PROP_WIDTH), ps);
			h = *lws_csp_px(lws_css_cascade_get_prop_atr(ctx,
							LCSP_PROP_HEIGHT), ps);

			if (!w.whole || !h.whole) {
				w = ((lws_dlo_t *)(u.u.dlo_jpeg))->box.w;
				h = ((lws_dlo_t *)(u.u.dlo_jpeg))->box.w;
			}

			lws_fx_add(&psb->curx, &psb->curx, &w);
			lws_fx_add(&psb->cury, &psb->cury, &h);
			if (lws_fx_comp(&psb->curx, &psb->widest) > 0)
				psb->widest = psb->curx;
		}
		break;

	case LHPCB_ELEMENT_END:
		if (ctx->npos == 2 && ctx->buf[0] == 'h' &&
		    ctx->buf[1] >= '0' && ctx->buf[1] <= '6') {
			newline(ctx, psb, ps, drt->dl);
			lws_fx_add(&psb->cury, &psb->cury,
				lws_csp_px(ps->css_padding[CCPAS_BOTTOM], ps));
			lws_fx_add(&psb->cury, &psb->cury,
				lws_csp_px(ps->css_margin[CCPAS_BOTTOM], ps));
		}

		if (ctx->npos == 3 && !strncmp(ctx->buf, "div", 2)) {
			lws_fx_t ox = ps->curx, w, wd;
			rect = (lws_dlo_rect_t *)ps->opaque1;

			if (lws_fx_comp(&ox, &ps->widest) > 0)
				ps->widest = ox;

			newline(ctx, ps, ps, drt->dl);

			lws_fx_add(&ps->cury, &ps->cury,
				lws_csp_px(ps->css_padding[CCPAS_BOTTOM], ps));

			/* move parent on according to used area plus bottom margin */

			if (psb && ps->css_position->propval != LCSP_PROPVAL_ABSOLUTE) {

				if (ps->css_display->propval == LCSP_PROPVAL_BLOCK) {
					lws_fx_add(&psb->cury, &psb->cury,
						   &ps->cury);
					lws_fx_add(&psb->cury, &psb->cury,
						   lws_csp_px(ps->css_margin[
						            CCPAS_BOTTOM], ps));
					lws_fx_set(psb->curx, 0, 0);
					lws_fx_set(psb->widest, 0, 0);
				} else
					lws_fx_add(&psb->curx, &psb->curx,
						   &ps->widest);

				if (rect)
					/* margin on the child was expressed as
					 * rel offset */
					lws_fx_add(&psb->curx, &psb->curx,
						   &rect->dlo.box.x);

				if (lws_fx_comp(&psb->curx, &psb->widest) > 0)
					psb->widest = psb->curx;
			}

			if (psb && ps->opaque1 &&
			    ps->css_margin[CCPAS_LEFT]->propval == LCSP_PROPVAL_AUTO &&
			    ps->css_margin[CCPAS_RIGHT]->propval == LCSP_PROPVAL_AUTO) {
				lws_dlo_rect_t *re = (lws_dlo_rect_t *)ps->opaque1;
				/* h-center a div... find the available h space first */
				w = ctx->ic.wh_px[LWS_LHPREF_WIDTH];
				if (psb->css_width &&
				    psb->css_width->propval != LCSP_PROPVAL_AUTO)
					w = *lws_csp_px(psb->css_width, psb);

				lws_fx_sub(&t1, &w, &re->dlo.box.w);
				lws_fx_div(&t1, &t1, &two);
				lws_fx_sub(&wd, &t1, &re->dlo.box.x);

				lws_fx_add(&re->dlo.box.x, &re->dlo.box.x, &wd);
			}

			/* fix up the dimensions of div rectangle */
			if (!rect)
				break;

			if (ps->css_height->propval == LCSP_PROPVAL_AUTO)
				w = ps->cury;
			else
				w = *lws_csp_px(ps->css_height, ps);

			rect->dlo.box.h = w;

			if (ps->css_width->propval == LCSP_PROPVAL_AUTO)
				w = ps->widest;
			else
				w = *lws_csp_px(ps->css_width, ps);

			lws_fx_add(&rect->dlo.box.w,
				lws_csp_px(ps->css_padding[CCPAS_RIGHT], ps), &w);

			if (psb && ps->css_position->propval != LCSP_PROPVAL_ABSOLUTE) {
				/* parent should account for our margin */
				lws_fx_add(&psb->curx, &psb->curx,
					lws_csp_px(ps->css_margin[CCPAS_RIGHT], ps));
				lws_fx_add(&psb->cury, &psb->cury,
					lws_csp_px(ps->css_margin[CCPAS_BOTTOM], ps));
			}

			if (psb) {
			lws_fx_add(&t1,
				lws_csp_px(ps->css_padding[CCPAS_RIGHT], ps),
								&psb->curx);
			lws_fx_add(&psb->widest, &t1, &w);
			}

			ps->opaque1 = NULL;
		}
		break;

	case LHPCB_CONTENT:

		if (!ps->css_display ||
		    ps->css_display->propval == LCSP_PROPVAL_NONE)
			break;

		if (ps->css_color)
			col = ps->css_color->u.rgba;

		a = lws_css_cascade_get_prop_atr(ctx, LCSP_PROP_FONT_SIZE);
		if (a)
			fc.fixed_height = (uint16_t)a->u.i.whole;

		a = lws_css_cascade_get_prop_atr(ctx, LCSP_PROP_FONT_FAMILY);
		if (a)
			fc.family_name = (const char *)&a[1];

		for (n = 0; n < ctx->npos; n++)
			if (ctx->buf[n] == '\n')
				s++;

		if (s == ctx->npos)
			return 0;

		/*
		 * Let's not deal with things off the bottom of the display
		 * surface.
		 */

		if (psb && psb->cury.whole > ctx->ic.wh_px[LWS_LHPREF_HEIGHT].whole)
			return 0;

		if (!psb)
			return 0;

		f = lws_font_choose(cx, &fc);

		n = s;
		while (n < ctx->npos) {
			int m;

			lws_fx_set(box.x, 0, 0);
			lws_fx_set(box.y, 0, 0);
			lws_fx_set(box.w, 0, 0);

			if (n == s && !(psb->runon & 1)) {
				lws_fx_set(indent, 0, 0);
			} else
				indent = psb->curx;
			lws_fx_add(&box.x, &indent,
					  lws_csp_px(ps->css_padding[CCPAS_LEFT], ps));
			lws_fx_add(&box.y, &box.y, &psb->cury);

			box.h.whole = (int32_t)f->choice.fixed_height;
			box.h.frac = 0;

			if (psb->css_width && psb->css_width->propval == LCSP_PROPVAL_AUTO) {
				//lws_fx_sub(&box.w, &ctx->ic.wh_px[0], &box.x);
				box.w = ctx->ic.wh_px[0];
			} else {
				lws_fx_sub(&t1, &psb->drt.w,
					   lws_csp_px(psb->css_padding[CCPAS_LEFT], psb));
				lws_fx_sub(&box.w, &t1,
					   lws_csp_px(psb->css_padding[CCPAS_RIGHT], psb));
			}

			if (!box.w.whole)
				lws_fx_sub(&box.w, &ctx->ic.wh_px[0], &box.x);
			assert(psb);

			txt = lws_display_dlo_text_new(drt->dl,
					(lws_dlo_t *)psb->opaque1, &box, f);
			if (!txt) {
				lwsl_err("%s: failed to alloc text\n", __func__);
				return 1;
			}
			runon(psb, &txt->dlo);
			txt->flags |= LWSDLO_TEXT_FLAG_WRAP;

//			a = lws_css_cascade_get_prop_atr(ctx, LCSP_PROP_TEXT_ALIGN);

			//lwsl_hexdump_notice(ctx->buf + n, (size_t)(ctx->npos - n));
			m = lws_display_dlo_text_update(txt, col, indent,
							ctx->buf + n,
							(size_t)(ctx->npos - n));
			if (m < 0) {
				lwsl_err("text_update ret %d\n", m);
				break;
			}

			n = (int)((size_t)n + txt->text_len);
			txt->dlo.box.w = txt->bounding_box.w;
			txt->dlo.box.h = txt->bounding_box.h;

			lws_fx_add(&psb->curx, &psb->curx, &txt->bounding_box.w);

			if (m > 0) { /* wrapping */
				newline(ctx, psb, ps, drt->dl);
				lws_fx_set(ps->curx, 0, 0);
				lws_fx_set(psb->curx, 0, 0);
				lws_fx_add(&ps->cury, &ps->cury, &txt->bounding_box.h);
			}
		}
		break;
	case LHPCB_COMMENT:
		break;
	}

	return 0;
}
